@echo off
setlocal
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" px_render
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component render -BuildDir "%CPP_BUILD_DIR%" -DistDir "build_official\%CPP_PRODUCT%\dist"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\check_no_qt.ps1" -Product "%CPP_PRODUCT%"
exit /b %errorlevel%
