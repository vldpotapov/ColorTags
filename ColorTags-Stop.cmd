@echo off
rem Stops the native overlay. Double-click to run.
setlocal
set "ROOT=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%src\VisualNative\Stop-ColorTagsOverlay.ps1"
if errorlevel 1 (
    echo.
    echo ---- failed, see the message above ----
    pause
)
