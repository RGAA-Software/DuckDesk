@echo off
setlocal
if "%~1"=="" (
    call "%~dp0scripts\build_cpp_target.bat" test_async_runtime test_message_notifier test_render_service_rpc_state test_rtc_ice_restart_workflow test_plugin_context_lifecycle test_notify_lifecycle test_relay_client_sdk_lifecycle
) else (
    call "%~dp0scripts\build_cpp_target.bat" %*
)
exit /b %errorlevel%
