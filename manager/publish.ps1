param(
 [Parameter(Mandatory=$true)][string]$Repository,
 [string]$Tag='v0.25.0',
 [string]$Output=(Join-Path $PSScriptRoot 'ready-to-upload')
)
$ErrorActionPreference='Stop'
$repo=$Repository.Trim().TrimEnd('/') -replace '^https://github.com/',''
if($repo -notmatch '^[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+$'){throw 'Use your-name/AnyAPI or the GitHub repository URL.'}
if($Tag -notmatch '^[A-Za-z0-9_.-]+$'){throw 'Invalid release tag.'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$catalogPath=Join-Path $PSScriptRoot 'publishing\catalog.json'
$catalog=Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
$catalog | Add-Member -NotePropertyName Repository -NotePropertyValue ('https://github.com/'+$repo) -Force
foreach($package in @($catalog.Api)+@($catalog.Mods)){
 $asset=$package.Name+'-'+$package.Version+'.zip'
 $file=Join-Path $PSScriptRoot ('publishing\assets\'+$asset)
 if(!(Test-Path -LiteralPath $file)){throw "Missing release package: $asset"}
 if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.Sha256){throw "Changed release package: $asset"}
 $package.Url='https://github.com/'+$repo+'/releases/download/'+$Tag+'/'+$asset
 Copy-Item -LiteralPath $file -Destination $Output
}
$catalog | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $catalogPath -Encoding UTF8
Copy-Item -LiteralPath $catalogPath -Destination (Join-Path $Output 'catalog.json')
& (Join-Path $PSScriptRoot 'build.ps1') -Output $Output
@"
1. Create a GitHub Release with tag $Tag.
2. Attach AnyAPI Manager.exe and all five ZIP files. Publish the release.
3. Upload catalog.json to the repository's main branch (top level).
4. Share the manager EXE. It includes only AnyAPI; mods download separately.

Repository: https://github.com/$repo
Catalog: https://raw.githubusercontent.com/$repo/main/catalog.json
"@ | Set-Content -LiteralPath (Join-Path $Output 'UPLOAD_STEPS.txt') -Encoding UTF8
Write-Output "Ready to upload: $Output"
