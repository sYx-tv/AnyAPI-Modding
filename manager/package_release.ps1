$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$release=Join-Path (Split-Path $PSScriptRoot -Parent) 'release'
$exe=Join-Path $PSScriptRoot 'output\AnyAPI Manager.exe'
$tests=Get-Content (Join-Path $PSScriptRoot 'output\SELF_TESTS.json') -Raw | ConvertFrom-Json
if(!$tests.Success){throw 'Manager checks must pass before packaging.'}
$catalog=Get-Content (Join-Path $PSScriptRoot 'publishing\catalog.json') -Raw | ConvertFrom-Json
if($catalog.Repository -match 'example-owner' -or ($catalog.Mods | Where-Object {$_.Url -match 'example-owner'})){throw 'Delivery must not contain placeholder repository URLs.'}
Copy-Item -LiteralPath $exe -Destination (Join-Path $release 'AnyAPI Manager.exe') -Force
$quick=@'
AnyAPI Manager

1. Extract this ZIP and open AnyAPI Manager.exe.
2. Your Anymaker Steam folder should appear automatically. Otherwise choose it in Settings.
3. Close the game and click Install API (or Reinstall API).
4. The project repository is preconfigured. Private downloads use your installed, signed-in GitHub CLI.
5. Open Mods, choose a mod and click Install. Choose Play with mods on Overview.
6. Play without mods pauses AnyAPI. Normal Steam launches stay in this mode until you choose Play with mods again.
7. Develop has searchable API documentation and an Export starter SDK button for making your own DLL mods.

This EXE includes only AnyAPI, not the optional mods. No terminal is needed.
The app does not need to stay open while playing. It backs up changed files and keeps mod settings.
API updates require a release verified for your installed game build.

The current repository is private: https://github.com/sYx-tv/AnyAPI-Modding. Only authorized accounts can download its mods or source. No token or credentials are included in the EXE.
'@
$quick | Set-Content -LiteralPath (Join-Path $release 'MANAGER_START_HERE.txt') -Encoding UTF8
function New-Archive($path,$files){
 $stream=[System.IO.File]::Open($path,[System.IO.FileMode]::Create)
 $zip=New-Object System.IO.Compression.ZipArchive($stream,[System.IO.Compression.ZipArchiveMode]::Create)
 try{foreach($entry in $files.GetEnumerator()){[System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$entry.Value,$entry.Key,[System.IO.Compression.CompressionLevel]::Optimal) | Out-Null}}finally{$zip.Dispose();$stream.Dispose()}
 $check=[System.IO.Compression.ZipFile]::OpenRead($path)
 try{if($check.Entries.Count -ne $files.Count){throw 'Archive file count mismatch.'};foreach($entry in $check.Entries){$input=$entry.Open();$hash=[System.Security.Cryptography.SHA256]::Create();try{$actual=([BitConverter]::ToString($hash.ComputeHash($input))).Replace('-','').ToLowerInvariant();if($actual -ne (Get-FileHash -LiteralPath $files[$entry.FullName] -Algorithm SHA256).Hash.ToLowerInvariant()){throw ('Archive mismatch: '+$entry.FullName)}}finally{$hash.Dispose();$input.Dispose()}}}finally{$check.Dispose()}
}
$download=Join-Path $release 'AnyAPI-Manager-1.1.0.zip'
New-Archive $download @{'AnyAPI Manager.exe'=$exe;'START_HERE.txt'=(Join-Path $release 'MANAGER_START_HERE.txt')}
$files=@{}
foreach($file in Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object {$_.Extension -in '.cs','.ps1','.md','.manifest','.ico','.py'}){$files['manager/'+$file.Name]=$file.FullName}
foreach($file in Get-ChildItem (Join-Path $PSScriptRoot 'publishing') -Recurse -File){$relative=$file.FullName.Substring($PSScriptRoot.Length+1).Replace('\','/');$files['manager/'+$relative]=$file.FullName}
foreach($file in Get-ChildItem (Join-Path $PSScriptRoot 'developer') -File){$files['manager/developer/'+$file.Name]=$file.FullName}
$files['.github/workflows/publish.yml']=Join-Path $PSScriptRoot '.github\workflows\publish.yml'
$files['README.md']=Join-Path $PSScriptRoot 'GITHUB_SETUP.md'
$files['catalog.json']=Join-Path $PSScriptRoot 'publishing\catalog.json'
foreach($name in @('AnyAPI-Current-Source-0.1.21-2026-10-06.zip','AnyAPI-SDK-0.1.21-2026-10-06.zip')){$files['developer/'+$name]=Join-Path $release $name}
$kit=Join-Path $release 'AnyAPI-GitHub-Publishing-Kit.zip'
New-Archive $kit $files
$packages=@{}
foreach($p in @($catalog.Api)+@($catalog.Mods)){$file=Join-Path $PSScriptRoot ('publishing\assets\'+$p.Name+'-'+$p.Version+'.zip');$hash=(Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant();if($hash -ne $p.Sha256){throw 'Package checksum mismatch.'};$packages[$p.Name]=$hash}
$assembly=[System.Reflection.Assembly]::LoadFile($exe)
$resources=$assembly.GetManifestResourceNames()
if(($resources | Sort-Object) -join ',' -ne 'api.zip,brand.ico,catalog.json,guide.json,starter-sdk.zip'){throw 'Unexpected embedded resource.'}
$stream=$assembly.GetManifestResourceStream('api.zip');$zip=New-Object System.IO.Compression.ZipArchive($stream,[System.IO.Compression.ZipArchiveMode]::Read)
try{if($zip.Entries.Count -ne 1 -or $zip.Entries[0].FullName -ne 'dinput8.dll'){throw 'Manager includes an unexpected payload.'}}finally{$zip.Dispose();$stream.Dispose()}
$onlinePath=Join-Path $PSScriptRoot 'output\ONLINE_CHECKS.json'
$online=if(Test-Path -LiteralPath $onlinePath){Get-Content -LiteralPath $onlinePath -Raw | ConvertFrom-Json}else{$null}
$evidence=@{manager_version='1.1.0';api_revision=25;game_version='0.1.21';steam_build=25725299;tests_passed=$tests.Passed;checks=$tests.Checks;embedded_mod_dlls=0;embedded_resources=$resources;repository=$catalog.Repository;repository_configured=([bool]$catalog.Repository);repository_visibility='PRIVATE';live_github_validation=$online;ui_pages_reviewed=@('Overview','Mods','Develop','Settings');starter_sdk_compiled='MSVC x64 Release; AnyAPI_ModInit export verified';actual_game_launch_tested=$false;package_sha256=$packages;artifacts_sha256=@{}}
foreach($file in @((Join-Path $release 'AnyAPI Manager.exe'),$download,$kit)){$evidence.artifacts_sha256[[System.IO.Path]::GetFileName($file)]=(Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()}
$evidence | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $release 'MANAGER_DELIVERY_EVIDENCE.json') -Encoding UTF8
Write-Output 'Verified manager download: EXE + instructions, zero optional mods.'
Write-Output 'Verified publishing kit: manager source, separate packages, native source and SDK.'
