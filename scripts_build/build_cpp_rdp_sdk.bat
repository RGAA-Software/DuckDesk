@echo off
setlocal
rem Pinned source dependency build only; no arguments required on a new checkout.
rem Optional: [FreeRdpSource] [BuildDirectory] [SdkDirectory] [VcpkgDirectory]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\prepare_rdp_sdk.ps1" -FreeRdpSource "%~1" -BuildDirectory "%~2" -SdkDirectory "%~3" -VcpkgDirectory "%~4"
exit /b %errorlevel%
