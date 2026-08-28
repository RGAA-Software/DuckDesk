@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0scripts\build_cpp_target.bat" px_sdk px_account_sdk px_profile_client px_console_client px_client_plugin px_net_plugin px_relay_client
exit /b %errorlevel%
