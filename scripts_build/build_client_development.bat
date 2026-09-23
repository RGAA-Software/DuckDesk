@echo off
setlocal

rem Complete runnable Client development distribution. This does not bump a
rem version and does not produce Official, Customer, OEM, or installer output.
if not "%~2"=="" (
    echo Usage: %~nx0 [jobs]
    exit /b 2
)

set "CPP_PRODUCT=client"
set "CPP_BUILD_DIR=build_official\client\cmake"
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"

set "RDP_SDK_DIRECTORY=%~dp0..\.cache\rdp_sdk"
if exist "%RDP_SDK_DIRECTORY%\px_rdp_sdk.json" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\verify_rdp_sdk.ps1" -SdkDirectory "%RDP_SDK_DIRECTORY%"
    if not errorlevel 1 (
        echo Reusing the verified pinned RDP SDK.
        goto build_client
    )
    echo The cached RDP SDK is stale or incomplete; rebuilding it.
)

call "%~dp0build_cpp_rdp_sdk.bat"
if errorlevel 1 exit /b %errorlevel%

:build_client
call "%~dp0..\scripts\build_cpp_target.bat" px_build_client_all
exit /b %errorlevel%
