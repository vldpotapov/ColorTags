# register.ps1 — register the ColorTags Explorer context menu.
#
# 1. Writes HKCU\Software\ColorTags config (PythonPath, ProjectRoot) for the DLL.
# 2. Packs + signs a self-signed MSIX (cert trusted in LocalMachine\TrustedPeople) and
#    installs it per-user — no Developer Mode / sideloading policy required.
# 3. Registers the IPropertyStore bridge (System.Keywords <-> :ColorTag):
#    - legacy shellex\PropertyHandler per-user (HKCU);
#    - CLSID + the MODERN path HKLM\...\PropertySystem\PropertyHandlers\<ext>
#      — the registrations Explorer itself can load (proven: shellex-registered
#      handlers are created+initialized but never read) — via ONE elevated import
#      (single UAC prompt). File types WITH a native handler are skipped
#      (a type can only have one handler).
#
# Usage:  .\register.ps1 [-RestartExplorer] [-Extensions @('.txt','.md',...)]

param(
    [switch]$RestartExplorer,
    [switch]$MenuOnly,
    [switch]$ValidatePackage,
    [switch]$ReconcilePropertyHandlers,
    [string]$SdkTools,
    [string]$PythonPath,
    [string[]]$Extensions = @(
        '.txt', '.md', '.log', '.ini', '.cfg', '.conf', '.csv', '.json', '.xml',
        '.html', '.htm', '.css', '.js', '.mjs', '.ts', '.tsx', '.jsx', '.py',
        '.cpp', '.hpp', '.h', '.c', '.cs', '.java', '.ps1', '.bat', '.cmd',
        '.vbs', '.zip', '.rar', '.7z', '.tar', '.gz', '.bz2', '.xz', '.iso',
        '.exe', '.dll', '.msi', '.mp4', '.mkv', '.avi', '.mov', '.wmv', '.flv',
        '.webm', '.mp3', '.wav', '.flac', '.ogg', '.aac', '.wma', '.bmp',
        '.gif', '.tiff', '.tif', '.webp', '.ico', '.svg', '.pdf', '.doc',
        '.xls', '.ppt', '.odt', '.ods', '.odp', '.epub', '.mobi', '.apk',
        '.deb', '.rpm', '.sql', '.db', '.dat', '.bin', '.bak', '.tmp',
        '.part', '.torrent', '.lnk'
    )
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out = Join-Path $root "out"
$archiveLayout = -not (Test-Path -LiteralPath (Join-Path $out 'AppxManifest.xml'))
if (-not (Test-Path -LiteralPath (Join-Path $out 'AppxManifest.xml'))) {
    $out = $root  # The release ZIP places built files beside register.ps1.
}
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $root)
$archiveRoot = Split-Path -Parent $root
$projectRoot = if ($archiveLayout -and
    (Test-Path -LiteralPath (Join-Path $archiveRoot 'src\Cli\colortag.py'))) {
    $archiveRoot
} elseif (-not $archiveLayout -and
    (Test-Path -LiteralPath (Join-Path $repositoryRoot 'src\Cli\colortag.py'))) {
    $repositoryRoot
} else {
    throw 'ColorTags Python modules are missing. Extract the entire release ZIP before registering.'
}
foreach ($required in @((Join-Path $out 'AppxManifest.xml'),
                       (Join-Path $out 'ColorTagsMenu.dll'),
                       (Join-Path $projectRoot 'src\Storage\ads_tag_store.py'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "ColorTags package is incomplete: $required"
    }
}
if ($ValidatePackage) {
    Write-Output "Package layout OK: $projectRoot"
    return
}
# makeappx.exe and signtool.exe come from the Windows SDK. The path was once
# hardcoded to one machine's temporary folder, which made the script useless
# anywhere else; the candidates below cover an installed SDK, one already on
# PATH, and that original folder, which is still where the tools live on the
# machine this was first written on.
function Resolve-SdkTools {
    param([string] $Preferred)

    $candidates = New-Object System.Collections.Generic.List[string]
    if ($Preferred) { $candidates.Add($Preferred) }

    $onPath = Get-Command 'makeappx.exe' -ErrorAction SilentlyContinue
    if ($onPath) { $candidates.Add((Split-Path -Parent $onPath.Source)) }

    $kitRoots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10\bin')
    ) | Where-Object { $_ -and (Test-Path $_) }
    foreach ($kitRoot in $kitRoots) {
        Get-ChildItem -Path $kitRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { $candidates.Add((Join-Path $_.FullName 'x64')) }
        $candidates.Add((Join-Path $kitRoot 'x64'))
    }

    # Portable copies of the signing tools, including the folder this project
    # used before the lookup existed.
    foreach ($portableRoot in @(
        (Join-Path $env:LOCALAPPDATA 'Temp\opencode\sdkbt\bin'),
        (Join-Path $env:TEMP 'opencode\sdkbt\bin'))) {
        if (-not (Test-Path $portableRoot)) { continue }
        Get-ChildItem -Path $portableRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { $candidates.Add((Join-Path $_.FullName 'x64')) }
    }

    foreach ($candidate in $candidates) {
        if (-not $candidate) { continue }
        if ((Test-Path (Join-Path $candidate 'makeappx.exe')) -and
            (Test-Path (Join-Path $candidate 'signtool.exe'))) {
            Write-Output "Signing tools: $candidate" | Out-Host
            return $candidate
        }
    }

    throw ("makeappx.exe and signtool.exe were not found. Looked in: " +
           ($candidates -join '; ') +
           ". Install the Windows 10/11 SDK (the 'Windows SDK Signing Tools' " +
           "component is enough) or pass -SdkTools <folder containing makeappx.exe>.")
}

$prebuilt = Test-Path -LiteralPath (Join-Path $out 'ColorTags.msix')
if (-not $prebuilt) { $tools = Resolve-SdkTools -Preferred $SdkTools }
$work = Join-Path $env:TEMP "colortags_msix"
New-Item -ItemType Directory -Path $work -Force | Out-Null

# Property handler CLSID (must match CLSID_TagsProperty in tags_menu.cpp)
$propClsid = "{C9E3056C-1843-4196-A716-83D5B77F8312}"

# 1. config for the DLL
$python = $null
if ($PythonPath -and -not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) {
    throw 'The supplied Python runtime is missing.'
}
$bundledPython = Join-Path $projectRoot 'Runtime\python.exe'
foreach ($commandName in @($PythonPath, $bundledPython, 'python', 'py')) {
    if (-not $commandName) { continue }
    $candidate = Get-Command $commandName -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $candidate) { continue }
    & $candidate.Source -c 'import sys; assert sys.version_info >= (3, 9)' 2>$null
    if ($LASTEXITCODE -eq 0) { $python = $candidate.Source; break }
    if ($PythonPath) { throw 'The supplied Python runtime failed validation.' }
}
if (-not $python) {
    throw 'ColorTags requires Python 3.9 or later. Make python or py available in PATH.'
}
if (-not (Test-Path 'HKCU:\Software\ColorTags')) {
    New-Item -Path 'HKCU:\Software\ColorTags' | Out-Null
}
Set-ItemProperty -Path "HKCU:\Software\ColorTags" -Name "PythonPath" -Value $python
Set-ItemProperty -Path "HKCU:\Software\ColorTags" -Name "ProjectRoot" -Value $projectRoot

# 2. A release contains a signed package and its PUBLIC certificate. Developer
# builds keep the original local pack/sign path. Private keys never ship.
if ($prebuilt) {
    $cer = Join-Path $out 'ColorTags.cer'
    if (-not (Test-Path -LiteralPath $cer)) { throw 'Package certificate is missing.' }
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cer)
    if ($cert.Subject -ne 'CN=ColorTags') { throw 'Unexpected package certificate publisher.' }
    $msix = Join-Path $out 'ColorTags.msix'
    $signature = Get-AuthenticodeSignature -FilePath $msix
    if (-not $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $cert.Thumbprint) {
        throw 'Menu package signature does not match its certificate.'
    }
} else {
# self-signed code-signing cert (reuse if present), trusted in
#    LocalMachine\TrustedPeople as required by MSIX deployment.
$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq "CN=ColorTags" -and $_.EnhancedKeyUsageList.ObjectId -contains "1.3.6.1.5.5.7.3.3" } |
    Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=ColorTags" `
        -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(5)
}
$cer = Join-Path $work "ColorTags.cer"
Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null
$pfx = Join-Path $work "ColorTags.pfx"
# Random per run. The certificate is self-signed, created here and used only
# to sign the local package, so the password protects nothing - but a fixed
# one in a public repository reads like a credential, and this costs nothing.
$pw = [System.Guid]::NewGuid().ToString('N') + '!Aa1'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password (ConvertTo-SecureString $pw -AsPlainText -Force) -Force | Out-Null

# 3. pack + sign the MSIX
$msix = Join-Path $work "ColorTags.msix"
& "$tools\makeappx.exe" pack /d $out /p $msix /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed ($LASTEXITCODE)" }
& "$tools\signtool.exe" sign /fd SHA256 /f $pfx /p $pw $msix
if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
Remove-Item -LiteralPath $pfx -Force
}

if (-not (Test-Path "Cert:\LocalMachine\TrustedPeople\$($cert.Thumbprint)")) {
    $trustProcess = Start-Process certutil.exe `
        -ArgumentList '-addstore', 'TrustedPeople', "`"$cer`"" `
        -Verb RunAs -Wait -PassThru -WindowStyle Hidden
    if ($trustProcess.ExitCode -ne 0) {
        throw "Failed to trust the package certificate (certutil exit $($trustProcess.ExitCode))."
    }
# Track only certificates trusted by this registration for precise cleanup.
$certificateKey = 'HKCU:\Software\ColorTags\PackageCertificates'
if (-not (Test-Path $certificateKey)) { New-Item -Path $certificateKey | Out-Null }
Set-ItemProperty -Path $certificateKey -Name $cert.Thumbprint -Value 1 -Type DWord
}
if ((Get-AuthenticodeSignature -FilePath $msix).Status -ne 'Valid') {
    throw 'Menu package signature validation failed after trusting its certificate.'
}

# 4. install per-user (remove any previous install of the same package first)
$old = Get-AppxPackage -Name "ColorTags.ExplorerMenu" -ErrorAction SilentlyContinue
if ($old) {
    Remove-AppxPackage -Package $old.PackageFullName
}
Add-AppxPackage -Path $msix

if ($MenuOnly) {
    Write-Output "Menu package registered; property-handler registrations were left unchanged."
    if ($RestartExplorer) {
        Write-Output "Restarting Explorer..."
        Stop-Process -Name explorer -Force
        Start-Sleep -Seconds 2
        Start-Process explorer
    }
    return
}

# 5. property handler bridge: CLSID + per-extension registration.
#    A file type can only have ONE property handler, so types that already
#    have a native one (Office, JPEG, MP3, PNG on 24H2+, ...) are skipped.
$pkg = Get-AppxPackage -Name "ColorTags.ExplorerMenu"
$dllPath = Join-Path $pkg.InstallLocation "ColorTagsMenu.dll"

# The property handler CLSID must NOT point into the package: WindowsApps
# denies DLL loads to non-package processes (E_ACCESSDENIED), which breaks
# the Tags column. Copy the DLL to a user-writable location and register
# that path instead. (The menu itself loads the packaged DLL via its
# manifest identity, so it is unaffected.)
$localDllDir = Join-Path $env:LOCALAPPDATA "Colortags"
New-Item -ItemType Directory -Path $localDllDir -Force | Out-Null
$dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.Substring(0, 12).ToLowerInvariant()
$localDll = Join-Path $localDllDir "ColorTagsMenu-$dllHash.dll"
if (-not (Test-Path -LiteralPath $localDll)) {
    Copy-Item -LiteralPath $dllPath -Destination $localDll
}
$dllPath = $localDll

function Test-NativePropertyHandler([string]$ext) {
    # A type can only have ONE property handler. Skip only when an existing
    # handler is NOT ours (re-registering our own CLSID is idempotent).
    $paths = @(
        "Registry::HKEY_CLASSES_ROOT\SystemFileAssociations\$ext\shellex\PropertyHandler",
        "Registry::HKEY_CLASSES_ROOT\$ext\shellex\PropertyHandler",
        "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\$ext"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) {
            $v = (Get-ItemProperty -Path $p -Name "(default)" -ErrorAction SilentlyContinue)."(default)"
            if ($v -and $v -ne $propClsid) { return $true }
        }
    }
    return $false
}

$clsidKey = "HKCU:\Software\Classes\CLSID\$propClsid"
New-Item -Path "$clsidKey\InprocServer32" -Force | Out-Null
Set-ItemProperty -Path "$clsidKey\InprocServer32" -Name "(default)" -Value $dllPath
Set-ItemProperty -Path "$clsidKey\InprocServer32" -Name "ThreadingModel" -Value "Apartment"
# Docs (prophand-reg-dist): handlers that support safe save declare ManualSafeSave=1.
Set-ItemProperty -Path $clsidKey -Name "ManualSafeSave" -Value 1 -Type DWord

$registered = 0
$skipped = 0
$regDllPath = $dllPath.Replace('\', '\\')
$regLines = @(
    "Windows Registry Editor Version 5.00",
    "",
    "[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$propClsid]",
    '"ManualSafeSave"=dword:00000001',
    "",
    "[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$propClsid\InprocServer32]",
    ('@="{0}"' -f $regDllPath),
    '"ThreadingModel"="Apartment"',
    ""
)
$machineChanges = 1
$desiredExtensions = @($Extensions | ForEach-Object {
    "." + $_.Trim().TrimStart('.').ToLowerInvariant()
} | Sort-Object -Unique)

if ($ReconcilePropertyHandlers) {
    $userRoot = "HKCU:\Software\Classes\SystemFileAssociations"
    $machineRoot = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers"
    $userOwned = @(Get-ChildItem $userRoot -ErrorAction SilentlyContinue |
        Where-Object {
            $p = Join-Path $_.PSPath "shellex\PropertyHandler"
            (Get-ItemProperty -Path $p -Name "(default)" -ErrorAction SilentlyContinue)."(default)" -eq $propClsid
        } | ForEach-Object PSChildName)
    $machineOwned = @(Get-ChildItem $machineRoot -ErrorAction SilentlyContinue |
        Where-Object {
            (Get-ItemProperty -Path $_.PSPath -Name "(default)" -ErrorAction SilentlyContinue)."(default)" -eq $propClsid
        } | ForEach-Object PSChildName)

    $backupDir = Join-Path $projectRoot "registry_backups\property-handlers"
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    $backupPath = Join-Path $backupDir ("before-column-spike-{0}.json" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
    [ordered]@{
        CreatedAt = (Get-Date).ToString("o")
        PropertyHandlerClsid = $propClsid
        UserExtensions = $userOwned
        MachineExtensions = $machineOwned
    } | ConvertTo-Json | Set-Content -LiteralPath $backupPath -Encoding UTF8
    Write-Output "PropertyHandler backup: $backupPath"

    foreach ($ext in $userOwned) {
        if ($desiredExtensions -notcontains $ext.ToLowerInvariant()) {
            Remove-Item -Path "$userRoot\$ext\shellex\PropertyHandler" -Recurse -Force
        }
    }
    foreach ($ext in $machineOwned) {
        if ($desiredExtensions -notcontains $ext.ToLowerInvariant()) {
            $regLines += "[-HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\$ext]"
            $regLines += ""
            $machineChanges++
        }
    }
}

foreach ($ext in $desiredExtensions) {
    if (Test-NativePropertyHandler $ext) { $skipped++; continue }
    $key = "HKCU:\Software\Classes\SystemFileAssociations\$ext\shellex\PropertyHandler"
    New-Item -Path $key -Force | Out-Null
    Set-ItemProperty -Path $key -Name "(default)" -Value $propClsid
    $regLines += ""
    $regLines += "[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\$ext]"
    $regLines += ('@="{0}"' -f $propClsid)
    $registered++
    $machineChanges++
}

# Explorer must be able to resolve both the modern handler mapping and its COM
# class from HKLM. Apply the CLSID and all extensions with one elevated import.
$hklmApplied = $machineChanges -eq 0
if ($machineChanges -gt 0) {
    $regFile = Join-Path $work "colortags_prophandlers.reg"
    $regLines | Set-Content -Path $regFile -Encoding Ascii
    try {
        $process = Start-Process reg.exe -ArgumentList 'import', "`"$regFile`"" -Verb RunAs -Wait -PassThru
        if ($process.ExitCode -ne 0) {
            throw "reg.exe exited with code $($process.ExitCode)"
        }
        $hklmApplied = $true
    } catch {
        throw "Property handler registration failed. Approve administrator access and run setup again. $($_.Exception.Message)"
    }
}

Write-Output "Registered."
Write-Output "  PythonPath = $python"
Write-Output "  ProjectRoot = $projectRoot"
Write-Output "  PropertyHandler: $registered extensions registered, $skipped skipped (native handler present)"
Write-Output "  Modern HKLM path (PropertySystem\PropertyHandlers): $hklmApplied (needs UAC approval)"

if ($RestartExplorer) {
    Write-Output "Restarting Explorer..."
    Stop-Process -Name explorer -Force
    Start-Sleep -Seconds 2
    Start-Process explorer
}
