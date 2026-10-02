param(
    [Parameter(Mandatory = $true)]
    [string]$Folder,
    [string]$CanonicalName = 'ColorTags.Explorer.ColorIcon',
    [ValidateSet('Add', 'Remove', 'List')]
    [string]$Mode = 'Add',
    [string]$ClangBin = (Join-Path $env:LOCALAPPDATA 'Programs\llvm-mingw\llvm-mingw-20260922-ucrt-x86_64\bin')
)

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'set_explorer_column.cpp'
$exe = Join-Path $env:TEMP 'colortags_set_explorer_column.exe'
$clang = Join-Path $ClangBin 'clang++.exe'

& $clang -O2 -std=c++17 -municode -DUNICODE -D_UNICODE $source -o $exe `
    -lole32 -loleaut32 -lpropsys -lshlwapi -luuid -static-libstdc++ -static-libgcc
if ($LASTEXITCODE -ne 0) { throw 'set-column helper compilation failed' }

& $exe (Resolve-Path -LiteralPath $Folder).Path $CanonicalName $Mode.ToLowerInvariant()
if ($LASTEXITCODE -ne 0) { throw "set-column helper failed ($LASTEXITCODE)" }

Remove-Item -LiteralPath $exe -Force
