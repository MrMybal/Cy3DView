# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
param([ValidateSet('Release','Debug')][string]$Config='Release',[switch]$SkipBuild)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(-not $SkipBuild){& (Join-Path $projectRoot 'build.ps1') -Config $Config}
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest=Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
$build=Join-Path $projectRoot "build\$Config"
& $ctest --test-dir $build --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Les tests doivent reussir avant la creation du paquet.'}
$portable=Join-Path $projectRoot 'dist\Cy3DView'
& $cmake --install $build --prefix $portable
if($LASTEXITCODE -ne 0){throw 'La preparation portable a echoue.'}
$smokeModel=Join-Path $projectRoot 'tests\fixtures\cube.obj'
$smokeOutput=Join-Path $build 'portable-check.png'
$smokeArguments='--smoke "{0}" "{1}"' -f $smokeModel,$smokeOutput
$check=Start-Process -FilePath (Join-Path $portable 'Cy3DView.exe') -ArgumentList $smokeArguments -WorkingDirectory $portable -WindowStyle Hidden -Wait -PassThru
if($check.ExitCode -ne 0){throw 'Le rendu de la version portable a echoue.'}
$pbrModel=Join-Path $portable 'samples\pbr_studio.gltf'
$pbrOutput=Join-Path $build 'portable-pbr-check.png'
$pbrArguments='--smoke "{0}" "{1}"' -f $pbrModel,$pbrOutput
$pbrCheck=Start-Process -FilePath (Join-Path $portable 'Cy3DView.exe') -ArgumentList $pbrArguments -WorkingDirectory $portable -WindowStyle Hidden -Wait -PassThru
if($pbrCheck.ExitCode -ne 0){throw 'Le rendu PBR du paquet portable a echoue.'}
foreach($sample in @('animated_flag.glb','colored_cloud.ply','gaussian_cloud.splat','usd_scene.usda')){
    $samplePath=Join-Path $portable "samples\$sample"
    $sampleOutput=Join-Path $build "portable-$sample.png"
    $sampleArguments='--smoke "{0}" "{1}"' -f $samplePath,$sampleOutput
    $sampleCheck=Start-Process -FilePath (Join-Path $portable 'Cy3DView.exe') -ArgumentList $sampleArguments -WorkingDirectory $portable -WindowStyle Hidden -Wait -PassThru
    if($sampleCheck.ExitCode -ne 0){throw "Le rendu portable de $sample a echoue."}
}
$projectDefinition=Get-Content (Join-Path $projectRoot 'CMakeLists.txt') -Raw
$match=[regex]::Match($projectDefinition,'project\(Cy3DView VERSION ([0-9.]+)')
if(-not $match.Success){throw 'Version du projet introuvable.'}
$version=$match.Groups[1].Value
$archive=Join-Path $projectRoot "dist\Cy3DView-$version-win64.zip"
Compress-Archive -Path $portable -DestinationPath $archive -Force
Write-Host "Version portable verifiee -> $archive"
