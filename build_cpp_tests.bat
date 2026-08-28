@echo off
setlocal
if "%~1"=="" (
    call "%~dp0scripts\build_cpp_target.bat" test_async_runtime test_message_notifier test_render_service_rpc_state
) else (
    call "%~dp0scripts\build_cpp_target.bat" %*
)
exit /b %errorlevel%
