param(
    [string]$ClangBin = (Join-Path $env:LOCALAPPDATA 'Programs\llvm-mingw\llvm-mingw-20260922-ucrt-x86_64\bin'),
    [ValidateSet('Minimal', 'Enum', 'IconList')]
    [string]$SchemaStage = 'Minimal'
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "property_schema_smoke.cpp"
$exe = Join-Path $env:TEMP "colortags_property_schema_smoke.exe"
$clang = Join-Path $ClangBin "clang++.exe"

& $clang -O2 -std=c++17 -municode -DUNICODE -D_UNICODE $source -o $exe `
    -lole32 -lpropsys -lshlwapi -luuid -static-libstdc++ -static-libgcc
if ($LASTEXITCODE -ne 0) { throw "schema smoke-test compilation failed" }

$testArgs = switch ($SchemaStage) {
    'Minimal'  { @('ColorTags.Explorer.ColorProbeString', '{828A77DC-07B7-4624-9BE0-253254F68A76}', 'String', '0') }
    'Enum'     { @('ColorTags.Explorer.ColorProbeEnum', '{6BE2AA1A-59B2-423D-BACF-21F577028CCD}', 'Enumeration', '0') }
    'IconList' { @('ColorTags.Explorer.ColorIcon', '{EB41E2DC-D0F2-40D3-A498-016A61147FE5}', 'Enumeration', '1') }
}
& $exe @testArgs
if ($LASTEXITCODE -ne 0) { throw "property schema smoke test failed ($LASTEXITCODE)" }

Remove-Item -LiteralPath $exe -Force
