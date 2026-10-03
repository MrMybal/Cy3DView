# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Cyberalien
param([string]$Compiler='', [switch]$AllowMissingOpenGL)
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
$renderStatus='passed'
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
    # Exercise the installed importer and its plugins independently of display drivers.
    $probe=Join-Path $installDir 'cy3d_probe.exe'
    $fixtures=Join-Path $projectRoot 'tests/fixtures'
    $probeOutput=Join-Path $testRoot 'installed-import.stdout.log'
    $probeError=Join-Path $testRoot 'installed-import.stderr.log'
    $process=Start-Process -FilePath $probe -ArgumentList ('--test "{0}"' -f $fixtures) -WorkingDirectory $installDir -WindowStyle Hidden -RedirectStandardOutput $probeOutput -RedirectStandardError $probeError -Wait -PassThru
    if($process.ExitCode -ne 0){
        Get-Content -LiteralPath $probeOutput,$probeError | Write-Output
        throw "Installed import test failed (exit $($process.ExitCode)). Logs: $testRoot"
    }
    Get-Content -LiteralPath $probeOutput | Select-Object -Last 1 | Write-Output
    $capture=Join-Path $testRoot 'installed-ui.png'
    $sample=Join-Path $installDir 'samples/pbr_studio.gltf'
    $renderOutput=Join-Path $testRoot 'installed-render.stdout.log'
    $renderError=Join-Path $testRoot 'installed-render.stderr.log'
    $process=Start-Process -FilePath $installedExe -ArgumentList ('--language fr --updates-smoke "{0}" "{1}"' -f $sample,$capture) -WorkingDirectory $installDir -WindowStyle Hidden -RedirectStandardOutput $renderOutput -RedirectStandardError $renderError -Wait -PassThru
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture)){
        $diagnostic=[string](Get-Content -LiteralPath $renderError -Raw)
        # Accept only GLFW's exact unsupported-driver diagnostics, with exit 1 and no capture.
        # Crashes, model errors, missing DLLs, timeouts and other GL errors still fail.
        $unsupported=@(
            'WGL: The driver does not appear to support OpenGL',
            'WGL: A forward compatible OpenGL context requested but WGL_ARB_create_context is unavailable',
            'WGL: OpenGL profile requested but WGL_ARB_create_context_profile is unavailable',
            'WGL: Driver does not support OpenGL version 3.3',
            'WGL: Driver does not support the requested OpenGL profile'
        )
        if($AllowMissingOpenGL -and $process.ExitCode -eq 1 -and -not (Test-Path -LiteralPath $capture) -and $diagnostic.Trim() -cin $unsupported){
            $renderStatus='unavailable (OpenGL 3.3 not supported by the test machine)'
            Write-Warning "Installed render check unavailable: $($diagnostic.Trim()). Installed imports and payload validation remain required."
        } else {
            Get-Content -LiteralPath $renderOutput,$renderError | Write-Output
            throw "Installed render/UI smoke failed (exit $($process.ExitCode)). Logs: $testRoot"
        }
    }
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
Write-Output "Installer, upgrade, installed imports, uninstall and user file preservation passed: $testRoot"
Write-Output "Installed render check: $renderStatus"
