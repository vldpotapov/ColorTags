@echo off
rem One double-click: restart the overlay on a fresh build, let it settle, then
rem write everything worth knowing into temp\diagnostics.txt.
rem
rem Exists so the state can be read directly instead of being relayed: several
rem rounds today were spent on a stale binary or a command that never ran, and
rem neither was visible from a screenshot.
setlocal
set "ROOT=%~dp0"
set "OUT=%ROOT%temp\diagnostics.txt"
if not exist "%ROOT%temp" mkdir "%ROOT%temp"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Set-Location -LiteralPath '%ROOT%';" ^
  "Start-Transcript -Path '%OUT%' -Force | Out-Null;" ^
  "try {" ^
  "  Write-Output ('collected: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'));" ^
  "  Write-Output ('git HEAD : ' + (git rev-parse --short HEAD 2>$null) + ' ' + (git log -1 --pretty=%%s 2>$null));" ^
  "  Write-Output '';" ^
  "  Write-Output '--- settings ---';" ^
  "  $cfg = Get-ItemProperty 'HKCU:\Software\ColorTags' -ErrorAction SilentlyContinue;" ^
  "  foreach ($n in 'DisplayMode','ColumnFormat','TraceFile','PythonPath','ProjectRoot') {" ^
  "    Write-Output ('  {0,-12} = {1}' -f $n, $cfg.$n) };" ^
  "  Write-Output '';" ^
  "  Write-Output '--- stopping the overlay ---';" ^
  "  try { & '%ROOT%src\VisualNative\Stop-ColorTagsOverlay.ps1' } catch { Write-Output ('  ' + $_.Exception.Message) };" ^
  "  Write-Output '--- building and starting ---';" ^
  "  & '%ROOT%src\VisualNative\build.ps1';" ^
  "  & '%ROOT%src\VisualNative\Start-ColorTagsOverlay.ps1' -StatusPath '%ROOT%temp\overlay-status.json' -FastStatus;" ^
  "  Write-Output '';" ^
  "  Write-Output '--- explorer windows and tabs, as the shell reports them ---';" ^
  "  $shell = New-Object -ComObject Shell.Application;" ^
  "  $i = 0; foreach ($w in @($shell.Windows())) {" ^
  "    $line = '  {0}: ' -f $i;" ^
  "    try { $line += ('hwnd=0x{0:X} path={1}' -f [int64]$w.HWND, $w.Document.Folder.Self.Path) }" ^
  "    catch { $line += '<unreadable>' };" ^
  "    Write-Output $line; $i++ };" ^
  "  Write-Output '';" ^
  "  Write-Output '--- letting it scan ---';" ^
  "  Start-Sleep -Seconds 8;" ^
  "  Write-Output '--- overlay status ---';" ^
  "  Get-Content -LiteralPath '%ROOT%temp\overlay-status.json' -Raw }" ^
  "catch { Write-Output ('FAILED: ' + $_.Exception.Message) }" ^
  "finally { Stop-Transcript | Out-Null }"
echo.
echo ---- written to %OUT% ----
type "%OUT%"
echo.
pause
