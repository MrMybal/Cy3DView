param([ValidateSet('Release','Debug')][string]$Config='Release',[switch]$Clean)
$ErrorActionPreference='Stop'
$env:VSLANG='1033'
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs) { throw 'Visual Studio avec les outils C++ est requis.' }
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build=Join-Path $PSScriptRoot "build\$Config"
$cleanArgument=if($Clean){'--clean-first'}else{''}
cmd /c "chcp 65001 >nul && `"$vcvars`" >nul && cmake -S `"$PSScriptRoot`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$Config && cmake --build `"$build`" --parallel 8 $cleanArgument"
if($LASTEXITCODE -ne 0) { throw 'La compilation a echoue.' }
Write-Host "OK -> $build\bin\Cy3DView.exe"
