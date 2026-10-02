@echo off
rem Chooses what the layer does while the list is scrolling, then restarts the
rem overlay so it takes effect.
rem
rem   hide    the layer comes off and returns once the list stops  (default)
rem   follow  the indicators stay up and move with the rows
rem
rem Usage:  ColorTags-ScrollBehavior.cmd [follow|hide]
setlocal
set "ROOT=%~dp0"
set "MODE=%~1"
if "%MODE%"=="" set "MODE=hide"
reg add "HKCU\Software\ColorTags" /v ScrollBehavior /t REG_SZ /d "%MODE%" /f >nul
echo Scroll behaviour: %MODE%
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { & '%ROOT%src\VisualNative\Stop-ColorTagsOverlay.ps1' } catch { };" ^
  "& '%ROOT%src\VisualNative\Start-ColorTagsOverlay.ps1' -StatusPath '%ROOT%temp\overlay-status.json'"
echo.
echo Compare with: ColorTags-ScrollBehavior.cmd follow
pause
