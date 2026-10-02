param(
    [string]$PackageRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'package'),
    [string]$Python = 'python'
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
$layoutResult = & (Join-Path $PackageRoot 'ShellExtension\register.ps1') -ValidatePackage
if ($layoutResult -ne "Package layout OK: $expectedRoot") {
    throw "Register script selected the wrong CLI root: $layoutResult"
}
Push-Location $PackageRoot
try {
    $colors = @(& $Python -m src.Cli.colortag list)
    if ($LASTEXITCODE -ne 0 -or $colors.Count -ne 7) {
        throw 'Bundled Python CLI did not list all seven colors.'
    }
} finally { Pop-Location }
Write-Output 'Release package smoke: PASS'
