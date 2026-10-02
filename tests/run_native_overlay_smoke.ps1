param(
    [string]$Folder = (Join-Path $env:USERPROFILE 'Downloads'),
    [int]$DurationSeconds = 5,
    [switch]$RequireDots
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $root 'src\VisualNative\out\ColorTagsOverlay.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'Build the native overlay before running this smoke test.'
}
$resolvedFolder = (Resolve-Path -LiteralPath $Folder).Path
$status = Join-Path $env:TEMP "colortags-native-smoke-$PID.json"
Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue

try {
    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--duration-seconds', $DurationSeconds,
        '--refresh-ms', 500,
        '--target-folder', ('"{0}"' -f $resolvedFolder),
        '--status', ('"{0}"' -f $status)
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1500
    if (-not (Test-Path -LiteralPath $status)) { throw 'Native overlay wrote no status.' }
    $state = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    if ($state.windows -lt 1) { throw "No Explorer window found for $resolvedFolder" }
    if ($RequireDots -and $state.dots -lt 1) { throw 'No tagged rows were rendered.' }
    if (-not $process.WaitForExit(($DurationSeconds + 5) * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw 'Native overlay did not exit after its bounded duration.'
    }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "Native overlay exit code: $($process.ExitCode)" }
    $unresponsive = @(Get-Process explorer | Where-Object { -not $_.Responding })
    if ($unresponsive.Count) { throw 'Explorer became unresponsive during smoke test.' }
    [pscustomobject]@{
        Windows = $state.windows
        Dots = $state.dots
        OverlayExitCode = $process.ExitCode
        ExplorerResponsive = $true
    }
} finally {
    Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue
}
