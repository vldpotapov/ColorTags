@echo off
rem Rebuilds the shell extension and registers it, so the context menu picks up
rem new icons or a new build. register.ps1 asks for elevation itself, and
rem restarts Explorer at the end.
rem
rem The icon cache is cleared first: Explorer keeps menu bitmaps between
rem sessions, so a rebuilt icon can otherwise keep showing its old colours.
setlocal
set "ROOT=%~dp0"
set "LOG=%ROOT%temp\menu-install-log.txt"
if not exist "%ROOT%temp" mkdir "%ROOT%temp"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; & '%ROOT%src\ShellExtension\build.ps1'; & '%ROOT%src\ShellExtension\register.ps1' -RestartExplorer" > "%LOG%" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%LOG%"
if not "%RESULT%"=="0" (
    echo.
    echo ---- failed, see above ----
    pause
    exit /b %RESULT%
)
ie4uinit.exe -show
echo.
echo Menu extension rebuilt and registered.
pause
