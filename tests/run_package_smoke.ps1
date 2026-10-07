param(
    [string]$PackageRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'package'),
    [string]$Python = 'python',
    [switch]$SelfContained
)

$ErrorActionPreference = 'Stop'
$expected = @(
    'Overlay\ColorTagsOverlay.exe',
    'ShellExtension\ColorTagsMenu.dll',
    'ShellExtension\AppxManifest.xml',
    'ShellExtension\register.ps1',
    'src\__init__.py',
    'src\Cli\colortag.py',
    'src\Core\tag_service.py',
    'src\Storage\ads_tag_store.py'
)
foreach ($relative in $expected) {
    $path = Join-Path $PackageRoot $relative
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing release file: $relative" }
}
if (Get-ChildItem -LiteralPath (Join-Path $PackageRoot 'ShellExtension') -Filter 'ColorTagsSchemaTool*.exe') {
    throw 'Experimental schema tool leaked into the release package.'
}

$expectedRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
if ($SelfContained) {
    foreach ($relative in @('Runtime\python.exe', 'Runtime\python314._pth',
                           'Runtime\LICENSE.txt', 'ShellExtension\ColorTags.msix',
                           'ShellExtension\ColorTags.cer')) {
        if (-not (Test-Path (Join-Path $expectedRoot $relative))) { throw "Missing installer dependency: $relative" }
    }
    if (Get-ChildItem $expectedRoot -Recurse -File -Include '*.pfx','*.pem','*.key') {
        throw 'Private signing material leaked into the package.'
    }
    $Python = Join-Path $expectedRoot 'Runtime\python.exe'
    $signature = Get-AuthenticodeSignature (Join-Path $expectedRoot 'ShellExtension\ColorTags.msix')
    $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2((Join-Path $expectedRoot 'ShellExtension\ColorTags.cer'))
    if (-not $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw 'MSIX signature does not match the shipped certificate.'
    }
}
$layoutResult = & (Join-Path $PackageRoot 'ShellExtension\register.ps1') -ValidatePackage
if ($layoutResult -ne "Package layout OK: $expectedRoot") {
    throw "Register script selected the wrong CLI root: $layoutResult"
}
Push-Location $PackageRoot
try {
    $colors = @(& $Python -B -m src.Cli.colortag list)
    if ($LASTEXITCODE -ne 0 -or $colors.Count -ne 7) {
        throw 'Bundled Python CLI did not list all seven colors.'
    }
} finally { Pop-Location }
Write-Output 'Release package smoke: PASS'

if ($SelfContained) {
    # Run from a foreign working directory: isolated Python must find app modules
    # without PATH, a system installation, site-packages or the repository cwd.
    $scratch = Join-Path $env:TEMP ('colortags-cli-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $scratch | Out-Null
    $oldLocalAppData = $env:LOCALAPPDATA
    $oldPath = $env:PATH
    Push-Location $scratch
    try {
        $env:LOCALAPPDATA = $scratch
        $env:PATH = "$env:SystemRoot\System32"
        $file = Join-Path $scratch 'tag test.txt'
        Set-Content -LiteralPath $file 'test'
        $before = (Get-Item $file).LastWriteTimeUtc
        & $Python -B -m src.Cli.colortag set $file blue
        if ($LASTEXITCODE -ne 0) { throw 'Bundled CLI set failed.' }
        $tag = & $Python -B -m src.Cli.colortag get $file
        if ($LASTEXITCODE -ne 0 -or $tag -ne 'Tag: blue') { throw 'Bundled CLI get failed.' }
        if ((Get-Item $file).LastWriteTimeUtc -ne $before) { throw 'Tagging changed modification time.' }
        & $Python -B -c 'import sqlite3, ctypes; c=sqlite3.connect(":memory:"); c.execute("select 1"); assert ctypes.windll.kernel32.GetCurrentProcessId()'
        if ($LASTEXITCODE -ne 0) { throw 'Bundled SQLite/ctypes runtime failed.' }
        & $Python -B -m src.Cli.colortag remove $file
        $tag = & $Python -B -m src.Cli.colortag get $file
        if ($LASTEXITCODE -ne 0 -or $tag -ne 'No tag') { throw 'Bundled CLI remove failed.' }
    } finally {
        Pop-Location
        $env:LOCALAPPDATA = $oldLocalAppData
        $env:PATH = $oldPath
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
    Write-Output 'Self-contained CLI/signature smoke: PASS'
}
