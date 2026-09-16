@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|remote
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node or remote.
    exit /b 2
)
call "%~dp0build_rust_target.bat" "%~1" service
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_rust_artifacts.ps1" -Component service -Product "%~1"
exit /b %errorlevel%
