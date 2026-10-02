param(
    [string]$ClangBin = (Join-Path $env:LOCALAPPDATA 'Programs\llvm-mingw\llvm-mingw-20260922-ucrt-x86_64\bin'),
    [string]$BuildDir = "$env:TEMP\colortags_native_overlay_build"
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
# The version the binary reports in its settings window. One file feeds the
# binary, the installer and the release, so they cannot disagree.
$versionFile = Join-Path (Split-Path -Parent (Split-Path -Parent $root)) 'VERSION'
$version = if (Test-Path -LiteralPath $versionFile) {
    (Get-Content -LiteralPath $versionFile -Raw).Trim()
} else {
    '0.0.0'
}
$out = Join-Path $root 'out'
$clang = Join-Path $ClangBin 'clang++.exe'
$rc = Join-Path $ClangBin 'llvm-rc.exe'
if (-not (Test-Path -LiteralPath $clang)) { throw "clang++ not found at $clang" }
if (-not (Test-Path -LiteralPath $rc)) { throw "llvm-rc not found at $rc" }

New-Item -ItemType Directory -Force -Path $BuildDir,$out | Out-Null
$temporaryExe = Join-Path $BuildDir 'ColorTagsOverlay.exe'

# The application icon, so the window, the task bar and the tray all show the
# same thing without the program drawing one at runtime.
$resource = Join-Path $BuildDir 'ColorTagsOverlay.res'
& $rc /fo $resource (Join-Path $root 'ColorTagsOverlay.rc')
if ($LASTEXITCODE -ne 0) { throw 'llvm-rc failed' }

# -mwindows puts the binary in the GUI subsystem. Without it the overlay is a
# console program: it inherits the console it was started from, dies with that
# window, and gets a console of its own when launched from Explorer.
& $clang -O2 -std=c++20 -municode -mwindows "-DCOLORTAGS_VERSION=$version" `
    (Join-Path $root 'ColorTagsOverlayNative.cpp') `
    $resource `
    -o $temporaryExe `
    -lole32 -loleaut32 -lshell32 -lshlwapi -luuid -luser32 -lgdi32 -ldwmapi `
    -lwinhttp `
    -static-libstdc++ -static-libgcc
if ($LASTEXITCODE -ne 0) { throw 'Native overlay build failed.' }

Copy-Item -LiteralPath $temporaryExe -Destination (Join-Path $out 'ColorTagsOverlay.exe') -Force
Write-Output "Build OK: $(Join-Path $out 'ColorTagsOverlay.exe')"
