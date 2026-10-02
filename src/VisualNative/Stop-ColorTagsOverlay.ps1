$ErrorActionPreference = 'Stop'
$pidPath = Join-Path $env:LOCALAPPDATA 'ColorTags\VisualNative\overlay.pid.json'
$statusPath = Join-Path $env:LOCALAPPDATA 'ColorTags\VisualNative\overlay-status.json'

# The stop event is what the overlay itself watches, and it exists whatever
# started the process — this script, a shortcut, or the .cmd in a downloaded
# release. The pid file only exists when Start-ColorTagsOverlay.ps1 launched it,
# so it is used to confirm the exit, never to decide whether anything is running.
$signalled = $false
try {
    $stopEvent = [System.Threading.EventWaitHandle]::OpenExisting(
        'Local\ColorTags.NativeOverlay.Stop'
    )
    $stopEvent.Set() | Out-Null
    $stopEvent.Dispose()
    $signalled = $true
} catch {
    # No event: nothing is holding it, so nothing is running.
}

$state = if (Test-Path -LiteralPath $pidPath) {
    try { Get-Content -LiteralPath $pidPath -Raw | ConvertFrom-Json } catch { $null }
} else { $null }
$process = if ($state -and $state.Pid) { Get-Process -Id $state.Pid -ErrorAction SilentlyContinue }
$sameProcess = $process -and $state.StartTimeUtcTicks -and
    [int64]$state.StartTimeUtcTicks -eq $process.StartTime.ToUniversalTime().Ticks
if ($sameProcess -and -not $process.WaitForExit(3000)) {
    # Only ever the PID whose start time was verified against the pid file.
    Stop-Process -Id $process.Id -Force
    $process.WaitForExit(3000) | Out-Null
}

Remove-Item -LiteralPath $pidPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $statusPath -Force -ErrorAction SilentlyContinue

if (-not $signalled) {
    Write-Output 'Native ColorTags overlay is not running.'
    exit 0
}

# Wait for any instance to notice the event, including one this script did not
# start and therefore cannot wait on by handle.
$deadline = (Get-Date).AddSeconds(5)
while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Name 'ColorTagsOverlay' -ErrorAction SilentlyContinue)) {
        Write-Output 'Native ColorTags overlay stopped.'
        exit 0
    }
    Start-Sleep -Milliseconds 150
}
Write-Warning 'The overlay was asked to stop but is still running.'
