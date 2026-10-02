<#
.SYNOPSIS
    Stages a release package from the two out\ directories.

.DESCRIPTION
    Run after both build scripts. Produces a folder that can be zipped as-is:
    the overlay with its start/stop scripts, the shell extension with its icons
    and registration scripts, and the documents a downloader needs.

    Nothing here compiles anything, so it can also be run locally to check what
    a release would contain.
#>
param(
    [string] $Destination = 'package'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$overlayOut = Join-Path $root 'src\VisualNative\out'
$shellOut = Join-Path $root 'src\ShellExtension\out'

foreach ($required in @(
    (Join-Path $overlayOut 'ColorTagsOverlay.exe'),
    (Join-Path $shellOut 'ColorTagsMenu.dll'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "missing build output: $required — run the build scripts first"
    }
}

if (Test-Path -LiteralPath $Destination) {
    Remove-Item -LiteralPath $Destination -Recurse -Force
}
$overlayDir = Join-Path $Destination 'Overlay'
$shellDir = Join-Path $Destination 'ShellExtension'
$sourceDir = Join-Path $Destination 'src'
New-Item -ItemType Directory -Force -Path $overlayDir, $shellDir, $sourceDir | Out-Null

# --- overlay ---------------------------------------------------------------
Copy-Item (Join-Path $overlayOut 'ColorTagsOverlay.exe') $overlayDir -Force
Copy-Item (Join-Path $root 'src\VisualNative\Stop-ColorTagsOverlay.ps1') $overlayDir -Force
Copy-Item (Join-Path $root 'src\VisualNative\Set-ColorTagsSettings.ps1') $overlayDir -Force

# Started directly rather than through the repository's build launchers: a
# downloaded package has no toolchain and nothing to build.
@'
@echo off
rem Starts the tag overlay. Its tray icon opens the settings window.
start "" "%~dp0ColorTagsOverlay.exe"
'@ | Set-Content -Path (Join-Path $overlayDir 'Start ColorTags.cmd') -Encoding ASCII

@'
@echo off
rem Asks the running overlay to shut down, and waits for it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Stop-ColorTagsOverlay.ps1"
'@ | Set-Content -Path (Join-Path $overlayDir 'Stop ColorTags.cmd') -Encoding ASCII

# --- shell extension -------------------------------------------------------
Copy-Item (Join-Path $shellOut '*') $shellDir -Recurse -Force
Get-ChildItem -LiteralPath $shellDir -Filter 'ColorTagsSchemaTool*.exe' -File |
    Remove-Item -Force
foreach ($script in @('register.ps1', 'unregister.ps1',
                      'register_icon_overlays.ps1', 'unregister_icon_overlays.ps1')) {
    Copy-Item (Join-Path $root "src\ShellExtension\$script") $shellDir -Force
}

# The Explorer menu invokes `python -m src.Cli.colortag` from ProjectRoot.
# Ship that module and its transitive local imports in the extracted archive.
Copy-Item (Join-Path $root 'src\__init__.py') $sourceDir -Force
foreach ($module in @('Cli', 'Core', 'Storage')) {
    $moduleDir = Join-Path $sourceDir $module
    New-Item -ItemType Directory -Force -Path $moduleDir | Out-Null
    Copy-Item (Join-Path $root "src\$module\*.py") $moduleDir -Force
}

# --- documents -------------------------------------------------------------
foreach ($document in @('README.md', 'INSTALL.md', 'LICENSE', 'CHANGELOG.md')) {
    $path = Join-Path $root $document
    if (Test-Path -LiteralPath $path) { Copy-Item $path $Destination -Force }
}

Get-ChildItem -Path $Destination -Recurse -File |
    ForEach-Object { $_.FullName.Substring((Resolve-Path $Destination).Path.Length + 1) } |
    Sort-Object |
    ForEach-Object { Write-Host "  $_" }
Write-Host "Package staged in $Destination"
