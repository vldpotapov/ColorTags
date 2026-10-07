# CI-only disposable Windows runner: do not overwrite an existing user install.
param([Parameter(Mandatory)][string]$Installer)
$ErrorActionPreference = 'Stop'
if (-not $env:GITHUB_ACTIONS) { throw 'Run this test only on a disposable GitHub Actions runner.' }
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\ColorTags-CI'
$setupLog = Join-Path $env:TEMP 'colortags-installer-smoke.log'
$setup = Start-Process -FilePath (Resolve-Path $Installer).Path `
    -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
        "/DIR=`"$installDir`"", "/LOG=`"$setupLog`"" -Wait -PassThru
if ($setup.ExitCode -ne 0) {
    Get-Content (Join-Path $env:LOCALAPPDATA 'Colortags\setup-integration.log') -ErrorAction SilentlyContinue
    throw "Installer failed: $($setup.ExitCode)"
}
try {
    $pkg = Get-AppxPackage -Name 'ColorTags.ExplorerMenu'
    if (-not $pkg) {
        Get-Content $setupLog -ErrorAction SilentlyContinue | Select-Object -Last 85
        Get-Content (Join-Path $env:LOCALAPPDATA 'Colortags\setup-integration.log') -ErrorAction SilentlyContinue
        Get-AppxPackage -AllUsers -Name 'ColorTags.ExplorerMenu' | Format-List Name,PackageUserInformation
        throw 'Installer did not register the menu package.'
    }
    $config = Get-ItemProperty 'HKCU:\Software\ColorTags'
    if ($config.ProjectRoot -ne $installDir -or $config.PythonPath -ne (Join-Path $installDir 'Runtime\python.exe')) {
        throw 'Installer registered external Python or the wrong application root.'
    }
    $handler = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{C9E3056C-1843-4196-A716-83D5B77F8312}\InprocServer32'
    if (-not (Test-Path $handler)) { throw 'Installer omitted the property handler.' }
    $dll = (Get-ItemProperty $handler).'(default)'
    if (-not (Test-Path -LiteralPath $dll)) { throw 'Property handler DLL does not exist.' }
    $certificateKey = Get-Item 'HKCU:\Software\ColorTags\PackageCertificates'
    $thumbprints = @($certificateKey.Property)
    foreach ($thumbprint in $thumbprints) {
        if (-not (Test-Path "Cert:\LocalMachine\TrustedPeople\$thumbprint")) { throw 'Package certificate was not trusted.' }
    }
    # Repair/update over the installed product must preserve label preferences.
    Set-ItemProperty 'HKCU:\Software\ColorTags' -Name 'DisplayMode' -Value 1 -Type DWord
    $update = Start-Process -FilePath (Resolve-Path $Installer).Path `
        -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$installDir`"" -Wait -PassThru
    if ($update.ExitCode -ne 0) { throw 'Installing over the existing version failed.' }
    if ((Get-ItemProperty 'HKCU:\Software\ColorTags').DisplayMode -ne 1) { throw 'Update reset display preferences.' }
    Write-Output 'Installed menu, runtime and column registration: PASS'
} finally {
    $uninstall = Start-Process (Join-Path $installDir 'unins000.exe') `
        -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    if ($uninstall.ExitCode -ne 0) { throw "Uninstaller failed: $($uninstall.ExitCode)" }
}
if (Get-AppxPackage -Name 'ColorTags.ExplorerMenu') { throw 'Menu package survived uninstall.' }
if (Test-Path $handler) { throw 'Property handler survived uninstall.' }
foreach ($thumbprint in $thumbprints) {
    if (Test-Path "Cert:\LocalMachine\TrustedPeople\$thumbprint") { throw 'Installer-owned certificate survived uninstall.' }
}
Write-Output 'Installer/uninstaller integration smoke: PASS'
