# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
param([string]$Compiler='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$testRoot=Join-Path $projectRoot ('build/installer-test-'+[guid]::NewGuid().ToString('N'))
$installDir=Join-Path $testRoot 'installed'
# An isolated installer identity prevents modifying the normal application's registration.
$uninstallKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6F7A52F0-1435-4F26-81A3-8157DFA865B9}_is1'
$openWithKey='HKCU:\Software\Classes\Applications\Cy3DView.InstallerTest.exe'
if(Test-Path -LiteralPath $uninstallKey){throw 'An installer test is already registered. Clean that test first.'}
$resolved=[IO.Path]::GetFullPath($installDir)
if(-not $resolved.StartsWith([IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Test folder must stay under build.'}
New-Item -ItemType Directory -Path $testRoot | Out-Null
& (Join-Path $PSScriptRoot 'build_installer.ps1') -Compiler $Compiler -OutputDir $testRoot -TestIdentity *> (Join-Path $testRoot 'compile.log')
$setup=Get-ChildItem -LiteralPath $testRoot -Filter '*-setup.exe' | Select-Object -First 1
if(-not $setup){throw 'Test installer not produced.'}
function Run-Setup([string]$Language){
    $arguments='/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOICONS /TASKS="" /LANG={0} /DIR="{1}" /LOG="{2}"' -f $Language,$installDir,(Join-Path $testRoot "install-$Language.log")
    $process=Start-Process -FilePath $setup.FullName -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0){throw "Installer returned $($process.ExitCode)."}
}
try {
    Run-Setup 'english'
    if(-not (Test-Path -LiteralPath $uninstallKey)){throw 'Windows uninstall registration missing.'}
    if(-not (Test-Path -LiteralPath $openWithKey)){throw 'Open with registration missing.'}
    $installedExe=Join-Path $installDir 'Cy3DView.exe'
    $definition=Get-Content (Join-Path $projectRoot 'CMakeLists.txt') -Raw
    $version=[regex]::Match($definition,'project\(Cy3DView VERSION ([0-9.]+)').Groups[1].Value
    if((Get-Item -LiteralPath $installedExe).VersionInfo.ProductVersion -ne $version){throw 'Installed version mismatch.'}
    if(-not (Test-Path -LiteralPath (Join-Path $installDir 'LICENSE'))){throw 'GPL license missing.'}
    $portable=Join-Path $projectRoot 'dist/Cy3DView'
    foreach($file in (Get-ChildItem -LiteralPath $portable -File -Recurse)){
        $relative=[IO.Path]::GetRelativePath($portable,$file.FullName)
        $installed=Join-Path $installDir $relative
        if(-not (Test-Path -LiteralPath $installed) -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $installed).Hash){throw "Installed payload differs: $relative"}
    }
    $settings=Join-Path $installDir 'Cy3DView.ini'
    $customPlugin=Join-Path $installDir 'plugins/user-plugin.txt'
    $customFile=Join-Path $installDir 'my-model.obj'
    Set-Content -LiteralPath $settings -Value "[Cy3DView]`nlanguage=fr" -NoNewline
    Set-Content -LiteralPath $customPlugin -Value 'Keep my plugin' -NoNewline
    Set-Content -LiteralPath $customFile -Value 'Keep my model' -NoNewline
    $originalSettings=(Get-FileHash -LiteralPath $settings).Hash
    Run-Setup 'french'
    if((Get-FileHash -LiteralPath $settings).Hash -ne $originalSettings){throw 'Upgrade changed preferences.'}
    if((Get-Content -LiteralPath $customPlugin -Raw) -ne 'Keep my plugin'){throw 'Upgrade changed user plugin.'}
    $capture=Join-Path $testRoot 'installed-ui.png'
    $sample=Join-Path $installDir 'samples/pbr_studio.gltf'
    $process=Start-Process -FilePath $installedExe -ArgumentList ('--language fr --updates-smoke "{0}" "{1}"' -f $sample,$capture) -WorkingDirectory $installDir -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture)){throw 'Installed render/UI smoke failed.'}
} finally {
    $uninstaller=Join-Path $installDir 'unins000.exe'
    if(Test-Path -LiteralPath $uninstaller){
        $process=Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -Wait -PassThru
        if($process.ExitCode -ne 0){throw 'Test uninstall failed.'}
    }
}
if((Test-Path -LiteralPath $uninstallKey) -or (Test-Path -LiteralPath $openWithKey)){throw 'Uninstall left its test registration.'}
if(Test-Path -LiteralPath (Join-Path $installDir 'Cy3DView.exe')){throw 'Uninstall left the application executable.'}
foreach($file in @($settings,$customPlugin,$customFile)){if(-not (Test-Path -LiteralPath $file)){throw 'Uninstall deleted a user file.'}}
Write-Output "Installer, upgrade, installed UI, uninstall and user file preservation passed: $testRoot"
