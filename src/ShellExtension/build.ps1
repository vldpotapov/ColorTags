# build.ps1 — build the ColorTags Explorer context menu extension.
#
# Requires a portable clang/llvm-mingw toolchain (no admin, no VS Build Tools).
# Default toolchain location can be overridden:  .\build.ps1 -ClangBin <dir>
#
# Note: the compiler binaries are blocked from writing under Documents
# (Windows protected-folder policy), so everything is compiled in a scratch
# dir under %TEMP% and the finished artifacts are copied into .\out.

param(
    [string]$ClangBin = (Join-Path $env:LOCALAPPDATA 'Programs\llvm-mingw\llvm-mingw-20260922-ucrt-x86_64\bin'),
    [string]$BuildDir = "$env:TEMP\colortags_build"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out = Join-Path $root "out"
$build = Join-Path $BuildDir "build"

New-Item -ItemType Directory -Force -Path $build | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $build "Assets") | Out-Null

$clang = Join-Path $ClangBin "clang++.exe"
$rc = Join-Path $ClangBin "llvm-rc.exe"
if (-not (Test-Path $clang)) { throw "clang++ not found at $clang" }
$mingwInc = Join-Path (Split-Path -Parent $ClangBin) "x86_64-w64-mingw32\include"
$clangInc = Join-Path (Split-Path -Parent $ClangBin) "include"

# 1. resources (msix identity manifest + version info)
& $rc /fo (Join-Path $build "tags_menu.res") `
    -I $mingwInc -I $clangInc (Join-Path $root "tags_menu.rc")
if ($LASTEXITCODE -ne 0) { throw "llvm-rc failed" }

# 2. COM server DLL (static C++ runtime — no DLL dependencies)
& $clang -shared -O2 -std=c++17 -DUNICODE -D_UNICODE `
    (Join-Path $root "tags_menu.cpp") `
    (Join-Path $build "tags_menu.res") `
    (Join-Path $root "tags_menu.def") `
    -o (Join-Path $build "ColorTagsMenu.dll") `
    -lole32 -lshell32 -lshlwapi -lpropsys -luuid -static-libstdc++ -static-libgcc
if ($LASTEXITCODE -ne 0) { throw "clang++ DLL failed" }

# 3. phantom executable required by the package manifest
& $clang -O2 -municode (Join-Path $root "ColorTagsStub.cpp") `
    -o (Join-Path $build "ColorTagsStub.exe")
if ($LASTEXITCODE -ne 0) { throw "clang++ stub failed" }

# 4. logo assets: the application icon, rendered from assets/app-icon.svg by
# tools/make_app_icon.py and committed next to this script.
Copy-Item (Join-Path $root "assets\Square44x44Logo.png") `
    (Join-Path $build "Assets\Square44x44Logo.png") -Force
Copy-Item (Join-Path $root "assets\Square150x150Logo.png") `
    (Join-Path $build "Assets\Square150x150Logo.png") -Force

# 5. package manifest
Copy-Item (Join-Path $root "AppxManifest.xml") (Join-Path $build "AppxManifest.xml") -Force

# 6. copy finished artifacts into the project
New-Item -ItemType Directory -Force -Path $out | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $out "Assets") | Out-Null
Copy-Item (Join-Path $build "ColorTagsMenu.dll") $out -Force
Copy-Item (Join-Path $build "ColorTagsStub.exe") $out -Force
Copy-Item (Join-Path $build "AppxManifest.xml") $out -Force
Copy-Item (Join-Path $build "Assets\*") (Join-Path $out "Assets") -Force

# The icon overlay handlers point the shell at a plain .ico beside the DLL
# rather than at a resource inside it, so the icons ship next to the binary.
$outIcons = Join-Path $out "icons"
New-Item -ItemType Directory -Force -Path $outIcons | Out-Null
Copy-Item (Join-Path $root "icons\*.ico") $outIcons -Force

Write-Output "Build OK: $out"
