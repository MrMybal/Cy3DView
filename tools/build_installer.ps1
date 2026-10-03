# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
param([string]$Compiler='', [string]$PackageDir='', [string]$OutputDir='', [switch]$TestIdentity)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(-not $PackageDir){$PackageDir=Join-Path $projectRoot 'dist/Cy3DView'}
if(-not $OutputDir){$OutputDir=Join-Path $projectRoot 'dist'}
if(-not $Compiler){
    $candidates=@(
        (Join-Path $projectRoot 'build/publish-tools/inno/compiler/ISCC.exe'),
        "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe",
        "$env:ProgramFiles/Inno Setup 7/ISCC.exe"
    )
    $Compiler=$candidates | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
    if(-not $Compiler){$command=Get-Command ISCC.exe -ErrorAction SilentlyContinue; if($command){$Compiler=$command.Source}}
}
if(-not $Compiler){throw 'Inno Setup 6.7+ is required. Install it from https://jrsoftware.org/isdl.php or pass -Compiler.'}
if(-not (Test-Path -LiteralPath (Join-Path $PackageDir 'Cy3DView.exe'))){throw 'Prepare the portable package first.'}
$project=Get-Content (Join-Path $projectRoot 'CMakeLists.txt') -Raw
$version=[regex]::Match($project,'project\(Cy3DView VERSION ([0-9.]+)').Groups[1].Value
if(-not $version){throw 'Project version missing.'}
$arguments=@("/DAppVersion=$version","/DPackageDir=$PackageDir","/DOutputDir=$OutputDir")
if($TestIdentity){$arguments+=@('/DAppIdentity={6F7A52F0-1435-4F26-81A3-8157DFA865B9}','/DRegistryName=Cy3DView.InstallerTest.exe')}
& $Compiler @arguments (Join-Path $projectRoot 'installer/Cy3DView.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed.'}
Write-Output (Join-Path $OutputDir "Cy3DView-$version-win64-setup.exe")
