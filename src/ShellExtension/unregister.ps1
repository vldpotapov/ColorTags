# unregister.ps1 — remove the ColorTags Explorer context menu extension.
#
# Removes the sparse package, the HKCU config, the per-user CLSIDs
# (in case DllRegisterServer was ever used), and the property handler
# registrations (only keys pointing at our CLSID — native handlers are
# never touched). Removing the machine-wide property-handler registrations
# requires one UAC prompt, matching register.ps1.

$ErrorActionPreference = "Stop"

$propClsid = "{C9E3056C-1843-4196-A716-83D5B77F8312}"

# 1. remove the sparse package
$pkg = Get-AppxPackage -Name "ColorTags.ExplorerMenu" -ErrorAction SilentlyContinue
if ($pkg) {
    Remove-AppxPackage -Package $pkg.PackageFullName
    Write-Output "Package removed: $($pkg.PackageFullName)"
} else {
    Write-Output "Package not installed (nothing to remove)."
}

# 2. Remove only menu integration settings. Labels and overlay preferences are
# user data and must survive removal or replacement of the shell extension.
foreach ($name in @('PythonPath', 'ProjectRoot')) {
    Remove-ItemProperty -Path 'HKCU:\Software\ColorTags' -Name $name `
        -ErrorAction SilentlyContinue
}

# 3. remove the per-user CLSIDs (fallback registration path)
Remove-Item -Path "HKCU:\Software\Classes\CLSID\{111B2250-529E-4194-AEE7-5DE3E7EAAC5C}" `
    -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$propClsid" `
    -Recurse -Force -ErrorAction SilentlyContinue

# 4. remove property handler registrations that point at our CLSID
$removed = 0
Get-ChildItem "HKCU:\Software\Classes\SystemFileAssociations" -ErrorAction SilentlyContinue |
    ForEach-Object {
        $ph = Join-Path $_.PSPath "shellex\PropertyHandler"
        if (Test-Path $ph) {
            $val = (Get-ItemProperty -Path $ph -Name "(default)" -ErrorAction SilentlyContinue)."(default)"
            if ($val -eq $propClsid) {
                Remove-Item -Path $ph -Recurse -Force
                $removed++
            }
        }
    }
if ($removed -gt 0) {
    Write-Output "PropertyHandler: removed $removed extension registrations."
}

# 5. remove the modern machine-wide registrations created by register.ps1.
# Build one .reg import so uninstall needs at most one UAC prompt.
$machineRoot = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers"
$machineKeys = @()
Get-ChildItem -Path $machineRoot -ErrorAction SilentlyContinue |
    ForEach-Object {
        $val = (Get-ItemProperty -Path $_.PSPath -Name "(default)" -ErrorAction SilentlyContinue)."(default)"
        if ($val -eq $propClsid) { $machineKeys += $_.Name }
    }
$machineClsidKey = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$propClsid"
$removeMachineClsid = Test-Path $machineClsidKey
if ($machineKeys.Count -gt 0 -or $removeMachineClsid) {
    $regFile = Join-Path $env:TEMP "colortags_remove_prophandlers.reg"
    $regLines = @("Windows Registry Editor Version 5.00", "")
    foreach ($keyName in $machineKeys) {
        $regLines += "[-$keyName]"
        $regLines += ""
    }
    if ($removeMachineClsid) {
        $regLines += "[-HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$propClsid]"
        $regLines += ""
    }
    $regLines | Set-Content -Path $regFile -Encoding Ascii
    $process = Start-Process reg.exe -ArgumentList 'import', "`"$regFile`"" -Verb RunAs -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Failed to remove machine-wide property-handler registrations (reg.exe exit $($process.ExitCode))."
    }
    Remove-Item -LiteralPath $regFile -Force -ErrorAction SilentlyContinue
    Write-Output "PropertyHandler: removed $($machineKeys.Count) extension registrations and machine CLSID=$removeMachineClsid."
}

# 6. remove installed support files and the certificates created by register.ps1.
$localDllDir = Join-Path $env:LOCALAPPDATA "Colortags"
Get-ChildItem -LiteralPath $localDllDir -Filter "ColorTagsMenu*.dll" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
$ownedThumbprints = @(Get-ChildItem Cert:\CurrentUser\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq "CN=ColorTags" } |
    ForEach-Object Thumbprint)
foreach ($store in @("Cert:\CurrentUser\My", "Cert:\CurrentUser\TrustedPeople", "Cert:\CurrentUser\Root")) {
    Get-ChildItem $store -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -eq "CN=ColorTags" } |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

$machineCertificates = @(Get-ChildItem Cert:\LocalMachine\TrustedPeople -ErrorAction SilentlyContinue |
    Where-Object { $_.Thumbprint -in $ownedThumbprints })
foreach ($certificate in $machineCertificates) {
    $process = Start-Process certutil.exe `
        -ArgumentList '-delstore', 'TrustedPeople', $certificate.Thumbprint `
        -Verb RunAs -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "Failed to remove machine certificate $($certificate.Thumbprint) (certutil exit $($process.ExitCode))."
    }
}

Write-Output "Unregistered."
