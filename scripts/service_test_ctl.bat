@echo off
rem Console-mode px_service start/stop helper for manual testing.
rem Usage:
rem   scripts\service_test_ctl.bat start [port]   (default port 20375)
rem   scripts\service_test_ctl.bat stop
rem   scripts\service_test_ctl.bat status
rem CMS link needs auth injection first, see docs/cms_app_schedule_state.md 7.1:
rem   node scripts\inject_service_auth.mjs --device-id e2e-machine-1 --appkey ... --cms-host 127.0.0.1 --cms-port 30500
setlocal
set "ACTION=%~1"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=20375"
set "DIST=%~dp0..\build_official\dist"
set "EXE=%DIST%\px_service.exe"

if /I "%ACTION%"=="start" goto :start
if /I "%ACTION%"=="stop" goto :stop
if /I "%ACTION%"=="status" goto :status
echo Usage: %~nx0 start^|stop^|status [port]
exit /b 1

:start
if not exist "%EXE%" (
    echo ERROR: %EXE% not found. Run build_official.bat first.
    exit /b 1
)
tasklist /FI "IMAGENAME eq px_service.exe" | findstr /I "px_service.exe" >nul
if not errorlevel 1 (
    echo px_service already running. Stop it first: %~nx0 stop
    exit /b 1
)
echo Starting px_service --console --port %PORT% (workdir=%DIST%)
start "px_service" /D "%DIST%" "%EXE%" --console --port %PORT%
exit /b 0

:stop
taskkill /F /IM px_service.exe >nul 2>&1
if errorlevel 1 (
    echo px_service not running.
) else (
    echo px_service stopped.
)
exit /b 0

:status
tasklist /FI "IMAGENAME eq px_service.exe" | findstr /I "px_service.exe" >nul
if errorlevel 1 (
    echo px_service: not running
    exit /b 1
)
tasklist /FI "IMAGENAME eq px_service.exe"
exit /b 0
