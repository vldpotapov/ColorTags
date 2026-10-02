@echo off
rem Builds the native overlay and starts it. Double-click to run.
rem Everything it prints goes to temp\start-log.txt, and the running overlay
rem writes temp\overlay-status.json so a failure can be diagnosed afterwards.
setlocal
set "ROOT=%~dp0"
set "LOG=%ROOT%temp\start-log.txt"
if not exist "%ROOT%temp" mkdir "%ROOT%temp"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; & '%ROOT%src\VisualNative\build.ps1'; & '%ROOT%src\VisualNative\Start-ColorTagsOverlay.ps1' -StatusPath '%ROOT%temp\overlay-status.json' -FastStatus" > "%LOG%" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%LOG%"
if not "%RESULT%"=="0" (
    echo.
    echo ---- failed, see above ----
    pause
)
