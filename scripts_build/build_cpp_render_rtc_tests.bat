@echo off
setlocal

rem Focused WebRTC transport security and lifecycle build. This never invokes build_official.bat.
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0..\scripts\build_cpp_target.bat" test_rtc_payload_authorization net_rtc check_cpp_ownership
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0..\build_official" -R "^rtc_payload_authorization$" --output-on-failure
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component render_network_libraries
exit /b %errorlevel%
