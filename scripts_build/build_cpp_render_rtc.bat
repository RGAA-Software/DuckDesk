@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0..\scripts\build_cpp_target.bat" net_rtc net_rtc_local
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component render_network_libraries
exit /b %errorlevel%
