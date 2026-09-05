@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"

rem px_file_transfer.proto generated objects cross these module boundaries.
rem Always build and publish this set atomically after changing the FT protocol.
call "%~dp0scripts\build_cpp_target.bat" px_panel px_render px_ft_engine net_ws net_relay net_rtc net_rtc_local net_udp px_client px_rtc_client
if errorlevel 1 exit /b %errorlevel%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component ft_protocol
exit /b %errorlevel%
