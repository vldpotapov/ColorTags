@echo off
rem One click: stops the running overlay, rebuilds it, starts it again.
rem Everything it prints goes to temp\rebuild-log.txt as well as to this window,
rem and the running overlay writes temp\overlay-status.json, so a failure can be
rem looked at afterwards.
setlocal
set "ROOT=%~dp0"
set "LOG=%ROOT%temp\rebuild-log.txt"
if not exist "%ROOT%temp" mkdir "%ROOT%temp"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; & '%ROOT%src\VisualNative\Stop-ColorTagsOverlay.ps1'; Start-Sleep -Milliseconds 400; & '%ROOT%src\VisualNative\build.ps1'; & '%ROOT%src\VisualNative\Start-ColorTagsOverlay.ps1' -StatusPath '%ROOT%temp\overlay-status.json' -FastStatus" > "%LOG%" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%LOG%"
if not "%RESULT%"=="0" (
    echo.
    echo ---- failed, see above ----
    pause
    exit /b %RESULT%
)
echo.
echo Overlay rebuilt and running. Tray icon: left click opens the settings window.
timeout /t 3 /nobreak >nul
