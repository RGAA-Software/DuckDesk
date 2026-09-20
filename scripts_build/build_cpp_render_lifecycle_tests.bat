@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|remote [jobs]
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node or remote.
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" test_captured_media_pipeline test_ws_callback_workflow test_media_recorder_sink test_was_audio_capture_source test_was_audio_capture_runtime test_miniaudio_reinit_cancel test_process_loopback_lifecycle test_render_execution_context_lifecycle test_render_context_shutdown test_callback_quiescence test_reconnect_supervisor test_udp_transport_shutdown test_relay_transport_reconnect_owner test_relay_resource_channel test_file_transfer_report_delivery test_relay_ws_reconnect test_ws_ipc_client_lifecycle test_sdk_websocket_reconnect test_webrtc_transport_lifecycle test_rtc_client_dll_lifecycle test_rdp_stream test_voice_call_runtime
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -L "^render-lifecycle$" --output-on-failure
exit /b %errorlevel%
