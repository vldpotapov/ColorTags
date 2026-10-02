param(
    [string]$TargetFolder,
    [int]$RefreshMilliseconds = 500,
    [ValidateRange(6, 32)]
    [int]$DotSize = 12,
    [ValidateSet('Tags', 'Color Tag')]
    [string]$ColumnName = 'Tags',
    # Where the overlay writes its status JSON. Point this somewhere readable
    # when diagnosing why nothing is drawn.
    [string]$StatusPath,
    # Write the status on every pass, not only on a full refresh.
    [switch]$FastStatus
)

$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'out\ColorTagsOverlay.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Native overlay is not built. Run $(Join-Path $PSScriptRoot 'build.ps1') first."
}

$runtimeDirectory = Join-Path $env:LOCALAPPDATA 'ColorTags\VisualNative'
$pidPath = Join-Path $runtimeDirectory 'overlay.pid.json'
$statusPath = if ($StatusPath) { $StatusPath } else { Join-Path $runtimeDirectory 'overlay-status.json' }
New-Item -ItemType Directory -Force -Path $runtimeDirectory | Out-Null

if (Test-Path -LiteralPath $pidPath) {
    $state = try { Get-Content -LiteralPath $pidPath -Raw | ConvertFrom-Json } catch { $null }
    $oldProcess = if ($state.Pid) { Get-Process -Id $state.Pid -ErrorAction SilentlyContinue }
    $sameProcess = $oldProcess -and $state.StartTimeUtcTicks -and
        [int64]$state.StartTimeUtcTicks -eq $oldProcess.StartTime.ToUniversalTime().Ticks
    if ($sameProcess) { throw "Native ColorTags overlay is already running (PID $($state.Pid))." }
    Remove-Item -LiteralPath $pidPath -Force -ErrorAction SilentlyContinue
}

$arguments = @(
    '--refresh-ms', $RefreshMilliseconds,
    '--dot-size', $DotSize,
    '--column', ('"{0}"' -f $ColumnName),
    '--status', ('"{0}"' -f $statusPath)
)
if ($FastStatus) { $arguments += '--diagnostic-fast-status' }
if ($TargetFolder) {
    $resolvedFolder = (Resolve-Path -LiteralPath $TargetFolder).Path
    $arguments += @('--target-folder', ('"{0}"' -f $resolvedFolder))
}

$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
[ordered]@{
    Pid = $process.Id
    StartTimeUtcTicks = $process.StartTime.ToUniversalTime().Ticks
    Executable = $executable
} | ConvertTo-Json | Set-Content -LiteralPath $pidPath -Encoding UTF8

Start-Sleep -Milliseconds ([Math]::Max(750, $RefreshMilliseconds * 2))
if ($process.HasExited) {
    # 9 is the overlay's own "someone else already owns the stop event" exit:
    # one instance at a time, whoever started it.
    if ($process.ExitCode -eq 9) {
        throw ("Another ColorTags overlay is already running (possibly one " +
               "started from a downloaded release). Stop it first: " +
               "Stop-ColorTagsOverlay.ps1 or ColorTags-Stop.cmd.")
    }
    throw "Native overlay stopped during startup (exit $($process.ExitCode))."
}
Write-Output "Native ColorTags overlay started (PID $($process.Id))."
Write-Output "Status: $statusPath"
