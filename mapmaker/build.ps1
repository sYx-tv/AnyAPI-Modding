# Builds "Anymaker Map Maker.exe" with the C# compiler that ships with Windows (.NET Framework 4.x),
# the same way as the manager. No SDK install needed.
param([string]$Output=(Join-Path $PSScriptRoot 'output'))
$ErrorActionPreference='Stop'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$exe=Join-Path $Output 'Anymaker Map Maker.exe'
$refs=@('System.dll','System.Core.dll','System.Drawing.dll','System.Windows.Forms.dll')
$compilerArgs=@('/nologo','/target:winexe','/platform:x64','/optimize+',('/out:'+$exe))
$compilerArgs += $refs | ForEach-Object {'/reference:'+$_}
$compilerArgs += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'editor') -Filter '*.cs' | ForEach-Object {$_.FullName}
& $compiler @compilerArgs
if($LASTEXITCODE -ne 0){throw 'Map Maker compilation failed.'}
Write-Output $exe
