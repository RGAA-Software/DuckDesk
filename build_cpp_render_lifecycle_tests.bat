@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0scripts\build_cpp_target.bat" test_captured_media_pipeline test_ws_callback_workflow test_media_recorder_sink test_live_pusher_sink test_was_audio_capture_source test_was_audio_capture_runtime test_miniaudio_reinit_cancel test_process_loopback_lifecycle test_plugin_context_lifecycle test_webrtc_libraries_lifecycle test_voice_call_runtime
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build_official" -L "^render-lifecycle$" --output-on-failure
exit /b %errorlevel%
