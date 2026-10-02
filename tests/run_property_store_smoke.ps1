param(
    [string]$ClangBin = (Join-Path $env:LOCALAPPDATA 'Programs\llvm-mingw\llvm-mingw-20260922-ucrt-x86_64\bin')
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot "property_store_smoke.cpp"
$dll = Join-Path $repo "src\ShellExtension\out\ColorTagsMenu.dll"
$exe = Join-Path $env:TEMP "colortags_property_store_smoke.exe"
$clang = Join-Path $ClangBin "clang++.exe"
$settingsKey = 'HKCU:\Software\ColorTags'
$settingsExisted = Test-Path -LiteralPath $settingsKey
$columnFormatExisted = $false
$originalColumnFormat = $null
$displayModeExisted = $false
$originalDisplayMode = $null

if ($settingsExisted) {
    $settingsItem = Get-Item -LiteralPath $settingsKey
    $columnFormatExisted = $settingsItem.GetValueNames() -contains 'ColumnFormat'
    if ($columnFormatExisted) {
        $originalColumnFormat = $settingsItem.GetValue('ColumnFormat')
    }
    $displayModeExisted = $settingsItem.GetValueNames() -contains 'DisplayMode'
    if ($displayModeExisted) {
        $originalDisplayMode = $settingsItem.GetValue('DisplayMode')
    }
} else {
    New-Item -Path $settingsKey -Force | Out-Null
}

if (-not (Test-Path -LiteralPath $dll)) {
    throw "Build the shell extension first: src\ShellExtension\build.ps1"
}

try {
    # The native test expects emoji in PKEY_Keywords. Make that expectation
    # independent of the user's current column-display preference.
    Set-ItemProperty -LiteralPath $settingsKey -Name ColumnFormat -Value emoji
    Set-ItemProperty -LiteralPath $settingsKey -Name DisplayMode -Type DWord -Value 1

    & $clang -O2 -std=c++17 -municode -DUNICODE -D_UNICODE $source -o $exe `
        -lole32 -lpropsys -luuid -luser32 -static-libstdc++ -static-libgcc
    if ($LASTEXITCODE -ne 0) { throw "native smoke-test compilation failed" }

    & $exe $dll
    if ($LASTEXITCODE -ne 0) { throw "property store smoke test failed ($LASTEXITCODE)" }
} finally {
    Remove-Item -LiteralPath $exe -Force -ErrorAction SilentlyContinue
    if ($columnFormatExisted) {
        Set-ItemProperty -LiteralPath $settingsKey -Name ColumnFormat `
            -Value $originalColumnFormat
    } else {
        Remove-ItemProperty -LiteralPath $settingsKey -Name ColumnFormat `
            -ErrorAction SilentlyContinue
    }
    if ($displayModeExisted) {
        Set-ItemProperty -LiteralPath $settingsKey -Name DisplayMode `
            -Type DWord -Value $originalDisplayMode
    } else {
        Remove-ItemProperty -LiteralPath $settingsKey -Name DisplayMode `
            -ErrorAction SilentlyContinue
    }
}
