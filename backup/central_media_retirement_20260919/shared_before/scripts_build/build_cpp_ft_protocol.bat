@echo off
setlocal
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"

rem px_file_transfer.proto generated objects cross these module boundaries.
rem Always build and publish this set atomically after changing the FT protocol.
call "%~dp0..\scripts\build_cpp_target.bat" px_panel px_render px_ft_engine net_ws net_relay net_rtc net_rtc_local net_udp px_client px_rtc_client
if errorlevel 1 exit /b %errorlevel%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component ft_protocol -BuildDir "%CPP_BUILD_DIR%" -DistDir "build_official\%CPP_PRODUCT%\dist"
exit /b %errorlevel%
