@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [target ...]
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="client" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node, client, or remote.
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if "%~2"=="" (
    call "%~dp0..\scripts\build_cpp_target.bat" test_async_runtime test_message_notifier test_render_service_rpc_state test_udp_media_state test_udp_media_failure test_render_execution_context_lifecycle test_notify_lifecycle test_relay_client_sdk_lifecycle
) else (
    call "%~dp0..\scripts\build_cpp_target.bat" %2 %3 %4 %5 %6 %7 %8 %9
)
exit /b %errorlevel%
