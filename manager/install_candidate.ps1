param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [Parameter(Mandatory=$true)][string]$ManagerExe,
    [string]$PackagesDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $PackagesDirectory) { $PackagesDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'release/AnyAPI-0.27.0-Candidate/manager-packages' }
Add-Type -AssemblyName System.Web.Extensions
Add-Type -AssemblyName System.IO.Compression.FileSystem
[void][Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $ManagerExe).Path)
$serializer = New-Object System.Web.Script.Serialization.JavaScriptSerializer
$catalog = $serializer.Deserialize([IO.File]::ReadAllText((Join-Path $PackagesDirectory 'catalog.json')), [AnyApiManager.Catalog])
[AnyApiManager.Rules]::Validate($catalog)
$gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
if ([AnyApiManager.Engine]::Running()) { throw 'Close Anymaker before installing.' }
$packages = @($catalog.Api) + @($catalog.Mods)
$allowed = @('anyapi','anyhelpers','anyinventory','anystorage','anymap','anygraphics')
if ($packages.Count -ne 6 -or @($packages | Where-Object { $_.Id -notin $allowed }).Count) { throw 'Expected the API and five standard mod packages.' }
foreach ($package in $packages) {
    if (-not [AnyApiManager.Rules]::Matches($package, [AnyApiManager.Rules]::Hash((Join-Path $gameRoot 'game.exe')), [AnyApiManager.Rules]::Hash((Join-Path $gameRoot 'bin/game.gcl')))) { throw 'Game build differs from candidate.' }
    [void][AnyApiManager.Engine]::ReadPackage($package, (Join-Path $PackagesDirectory ($package.Name + '-' + $package.Version + '.zip')))
}
$relativeFiles = @('AnyAPI and Modding/.manager/installed.json','AnyAPI and Modding/.manager/paused-dinput8.dll')
foreach ($package in $packages) { foreach ($relative in $package.FileHashes.Keys) { $relativeFiles += $relative; if ($package.Id -ne 'anyapi') { $relativeFiles += $relative + '.disabled' } } }
$incompatible = [AnyApiManager.Engine]::Incompatible($gameRoot,$catalog)
foreach ($relative in $incompatible.Keys) { if ($relative -notin $relativeFiles) { throw ('Another mod needs separate compatibility review: ' + $relative) } }
$backup = Join-Path $PackagesDirectory ('install-backups/' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $backup | Out-Null
$before = @{}
foreach ($relative in $relativeFiles) {
    $path = [AnyApiManager.Rules]::Target($gameRoot,$relative)
    $before[$relative] = Test-Path -LiteralPath $path -PathType Leaf
    if ($before[$relative]) {
        $destination = Join-Path $backup $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
        Copy-Item -LiteralPath $path -Destination $destination
    }
}
$before | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'original-files.json') -Encoding UTF8
function Apply-Candidate($action,$package,$zip) {
    $request = New-Object AnyApiManager.ApplyRequest
    $request.GamePath=$gameRoot; $request.Action=$action; $request.Package=$package
    $request.ZipPath=$zip; $request.Catalog=$catalog
    $request.ReplaceUnknown=$true; $request.DisableIncompatible=$true
    $result=[AnyApiManager.Engine]::Apply($request,$null,$false)
    if (-not $result.Success) { throw $result.Message }
    Write-Output $result.Message
}
try {
    # Migrate imported copies of these same mods to official package identities.
    $installed=[AnyApiManager.Engine]::ReadInstalled($gameRoot)
    foreach ($receipt in @($installed.Packages.Values)) {
        if ($receipt.Package.Local -and @($receipt.Package.FileHashes.Keys | Where-Object { $_ -in $relativeFiles }).Count) {
            Apply-Candidate 'remove-local' $receipt.Package $null
        }
    }
    foreach ($package in $packages) { Apply-Candidate 'install' $package (Join-Path $PackagesDirectory ($package.Name + '-' + $package.Version + '.zip')) }
    if ([AnyApiManager.Engine]::Incompatible($gameRoot,$catalog).Count) { throw 'An installed mod is still incompatible.' }
    foreach ($package in $packages) { if ([AnyApiManager.Engine]::Status($gameRoot,$package) -ne 'Installed') { throw ('Installed checksum differs: ' + $package.Name) } }
    $record = @{ Api='0.27.0'; Game='0.1.23'; Backup=$backup; GameDirectory=$gameRoot; GameplayAcceptance='PENDING'; Mods=@($catalog.Mods | ForEach-Object { $_.Name }) }
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'installation.json') -Encoding UTF8
    Write-Output ('Installed and checksum-verified. Backup: ' + $backup)
} catch {
    $failure=$_
    if ([AnyApiManager.Engine]::Running()) { throw ('Installation failed. Close the game before restoring the backup: ' + $backup + '. ' + $failure) }
    foreach ($relative in $relativeFiles) {
        $path=[AnyApiManager.Rules]::Target($gameRoot,$relative)
        if ($before[$relative]) { Copy-Item -LiteralPath (Join-Path $backup $relative) -Destination $path -Force }
        elseif (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
    }
    throw ('Installation rolled back: ' + $failure)
}
