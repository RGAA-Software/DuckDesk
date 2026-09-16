@echo off
setlocal
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote target [jobs]
    exit /b 2
)
if "%~2"=="" (
    echo Usage: %~nx0 cloud_node^|remote target [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
set "LIBRARY_TARGET=%~2"
if not "%~3"=="" set "CPP_BUILD_JOBS=%~3"
call "%~dp0..\scripts\build_cpp_target.bat" %LIBRARY_TARGET%
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component render_network_library -LibraryTarget "%LIBRARY_TARGET%" -BuildDir "%CPP_BUILD_DIR%" -DistDir "build_official\%CPP_PRODUCT%\dist"
exit /b %errorlevel%
