param([string]$Output=(Join-Path $PSScriptRoot 'output'))
$ErrorActionPreference='Stop'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Add-Type -AssemblyName System.Drawing
$iconPath=Join-Path $PSScriptRoot 'brand.ico'
if(!(Test-Path -LiteralPath $iconPath)){
 $image=New-Object System.Drawing.Bitmap 64,64
 $graphics=[System.Drawing.Graphics]::FromImage($image)
 $graphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
 $graphics.Clear([System.Drawing.Color]::FromArgb(25,31,40))
 $brush=New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(72,149,242))
 $font=New-Object System.Drawing.Font 'Segoe UI',40,([System.Drawing.FontStyle]::Bold),([System.Drawing.GraphicsUnit]::Pixel)
 $graphics.DrawString('A',$font,$brush,([single]13),([single]5))
 $icon=[System.Drawing.Icon]::FromHandle($image.GetHicon())
 $file=[System.IO.File]::Create($iconPath);$icon.Save($file);$file.Dispose();$icon.Dispose();$font.Dispose();$brush.Dispose();$graphics.Dispose();$image.Dispose()
}
$refs=@('System.dll','System.Core.dll','System.Drawing.dll','System.Windows.Forms.dll','System.Net.Http.dll','System.Web.Extensions.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll')
$compilerArgs=@('/nologo','/target:winexe','/platform:x64','/optimize+')
$compilerArgs += '/win32manifest:'+(Join-Path $PSScriptRoot 'app.manifest')
$compilerArgs += '/out:'+(Join-Path $Output 'AnyAPI Manager.exe')
$compilerArgs += '/win32icon:'+$iconPath
$compilerArgs += '/resource:'+$iconPath+',brand.ico'
$compilerArgs += '/resource:'+(Join-Path $PSScriptRoot 'publishing\assets\AnyAPI-0.27.0.zip')+',api.zip'
$compilerArgs += '/resource:'+(Join-Path $PSScriptRoot 'publishing\catalog.json')+',catalog.json'
$compilerArgs += '/resource:'+(Join-Path $PSScriptRoot 'developer\guide.json')+',guide.json'
$compilerArgs += '/resource:'+(Join-Path $PSScriptRoot 'developer\starter-sdk.zip')+',starter-sdk.zip'
$compilerArgs += $refs | ForEach-Object {'/reference:'+$_}
$compilerArgs += Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cs' | ForEach-Object {$_.FullName}
& $compiler @compilerArgs
if($LASTEXITCODE -ne 0){throw 'Manager compilation failed.'}
Write-Output (Join-Path $Output 'AnyAPI Manager.exe')
