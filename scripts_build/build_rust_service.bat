@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|remote [jobs] [development^|official]
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node or remote.
    exit /b 2
)
set "RUST_DISTRIBUTION=development"
if not "%~3"=="" set "RUST_DISTRIBUTION=%~3"
call "%~dp0build_rust_target.bat" "%~1" service "%~2" "%RUST_DISTRIBUTION%"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_rust_artifacts.ps1" -Component service -Product "%~1" -Distribution "%RUST_DISTRIBUTION%"
exit /b %errorlevel%
