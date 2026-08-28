@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0scripts\build_cpp_target.bat" enc_amf clipboard cap_dda event_replayer enc_ffmpeg frame_carrier frame_debugger frame_resizer ft cap_gdi joystick live_pusher media_recorder mock_video_stream net_relay net_rtc net_rtc_local net_udp net_ws enc_nvenc obj_detector enc_opus voice_call cap_was_audio
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component render_plugins
exit /b %errorlevel%
