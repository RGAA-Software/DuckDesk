@echo off
setlocal
rem Pinned third-party SDK build/install only. Source is explicit and read-only.
rem Usage: build_cpp_rdp_sdk.bat FreeRdpSource [BuildDirectory] [SdkDirectory] [VcpkgDirectory]
if "%~1"=="" (
    echo Usage: build_cpp_rdp_sdk.bat FreeRdpSource [BuildDirectory] [SdkDirectory] [VcpkgDirectory]
    exit /b 2
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\prepare_rdp_sdk.ps1" -FreeRdpSource "%~1" -BuildDirectory "%~2" -SdkDirectory "%~3" -VcpkgDirectory "%~4"
exit /b %errorlevel%
