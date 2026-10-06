param(
 [string]$Exe=(Join-Path $PSScriptRoot 'ready-to-upload\AnyAPI Manager.exe'),
 [string]$Repository='sYx-tv/AnyAPI-Modding',
 [string]$Output=(Join-Path $PSScriptRoot 'ready-to-upload\ONLINE_CHECKS.json')
)
$ErrorActionPreference='Stop'
# Run in Windows PowerShell 5.1 so these checks use the manager's .NET Framework runtime.
Add-Type -AssemblyName System.Net.Http,System.Web.Extensions,System.IO.Compression,System.IO.Compression.FileSystem
[System.Reflection.Assembly]::LoadFile([System.IO.Path]::GetFullPath($Exe)) | Out-Null
$checks=@()
$catalog=[AnyApiManager.Engine]::Fetch($Repository).GetAwaiter().GetResult()
$embedded=[AnyApiManager.Engine]::Bundled()
if($embedded.Repository -ne ('https://github.com/'+$Repository)){throw 'Manager default repository does not match.'}
$checks+='Standalone manager defaults to the published repository'
if($catalog.Mods.Count -lt 4 -or $catalog.Api.Count -lt 1){throw 'Published catalog is incomplete.'}
$checks+='Live catalog fetch and schema validation (private GitHub CLI access supported)'
$folder=Join-Path (Split-Path ([System.IO.Path]::GetFullPath($Output)) -Parent) ('online-check-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder | Out-Null
try{
 foreach($package in @($catalog.Api)+@($catalog.Mods)){
  $bytes=[AnyApiManager.Engine]::DownloadBytes($package.Url,67108864,$null).GetAwaiter().GetResult()
  $zip=Join-Path $folder ($package.Id+'.zip')
  [System.IO.File]::WriteAllBytes($zip,$bytes)
  $files=[AnyApiManager.Engine]::ReadPackage($package,$zip)
  if($files.Count -ne 1){throw 'Unexpected package payload.'}
  $checks+=($package.Name+': live download, ZIP checksum, DLL checksum, path and x64 validation')
 }
 $result=@{Success=$true;Passed=$checks.Count;Repository=('https://github.com/'+$Repository);Checks=$checks;GameFilesChanged=$false}
 $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding UTF8
 $result | ConvertTo-Json -Depth 5
}finally{
 $parent=[System.IO.Path]::GetFullPath((Split-Path $Output -Parent))
 $resolved=[System.IO.Path]::GetFullPath($folder)
 if($resolved.StartsWith($parent+'\',[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
