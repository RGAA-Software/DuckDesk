@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0scripts\build_cpp_target.bat" px_sdk px_account_sdk px_profile_client px_console_client px_net_plugin px_relay_client test_sdk_websocket_reconnect test_relay_ws_reconnect
exit /b %errorlevel%
