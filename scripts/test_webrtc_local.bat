@echo off
setlocal enabledelayedexpansion

rem ============================================================================
rem test_webrtc_local.bat
rem
rem Launch GammaRayClientInner.exe in "WebRTC Local" mode against a render on
rem the LAN (default: this machine, 127.0.0.1:20371), bypassing the panel.
rem
rem Usage:
rem   scripts\test_webrtc_local.bat [remote_device_id] [random_pwd] [host] [port]
rem
rem   remote_device_id  device id of the render (default: 600378210, this PC)
rem   random_pwd        the render's random password in PLAIN text (from the
rem                     CMS device list / panel). Leave empty when the render
rem                     has neither a safety password nor a random password.
rem   host / port       render address (default: 127.0.0.1 / 20371)
rem
rem What to verify:
rem   1. The client window shows the remote desktop, mouse/keyboard work.
rem   2. Statistics panel (float controller) shows connection type
rem      "WebRTC Local".
rem   3. Logs: %PROGRAMDATA%\GammaRay\px_logs\app.rtc.*.log shows
rem      ICE gathering complete -> answer sdp set -> ICE Connected -> frames.
rem
rem Notes:
rem   - Requires the render to encode H264 (net_rtc_local negotiates H264 only).
rem   - If the same device_id:stream_id is already connected (e.g. a web_client
rem     session), the client automatically retries once with takeover=1.
rem ============================================================================

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"

set "CLIENT_EXE=%REPO_ROOT%\build_official\dist\GammaRayClientInner.exe"
if not exist "%CLIENT_EXE%" (
    echo ERROR: %CLIENT_EXE% not found. Run build_official.bat first.
    exit /b 1
)

rem --- arguments -------------------------------------------------------------
set "REMOTE_DEVICE_ID=%~1"
if "%REMOTE_DEVICE_ID%"=="" set "REMOTE_DEVICE_ID=600378210"

set "RANDOM_PWD=%~2"

set "HOST=%~3"
if "%HOST%"=="" set "HOST=127.0.0.1"

set "PORT=%~4"
if "%PORT%"=="" set "PORT=20371"

rem --- base64-encode the plain password for --remote_device_rp ----------------
set "RP_B64="
if not "%RANDOM_PWD%"=="" (
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('%RANDOM_PWD%'))"`) do set "RP_B64=%%i"
)

rem --- fixed test parameters (mirror running_stream_manager.cpp defaults) -----
set "VISITOR_DEVICE_ID=900000001"
set "STREAM_ID=webrtc_local_test1"

rem --- optional 5th arg: split windows (multi-screen, one window per monitor) -
set "SPLIT_WINDOWS=%~5"
if "%SPLIT_WINDOWS%"=="" set "SPLIT_WINDOWS=false"

echo ============================================
echo WebRTC Local test
echo   client : %CLIENT_EXE%
echo   target : %HOST%:%PORT%  device %REMOTE_DEVICE_ID%
echo   stream : %STREAM_ID%
echo ============================================

"%CLIENT_EXE%" ^
    --host=%HOST% ^
    --port=%PORT% ^
    --appkey=test_appkey ^
    --spvr_host=127.0.0.1 ^
    --spvr_port=30500 ^
    --audio=1 ^
    --clipboard=1 ^
    --stream_id=%STREAM_ID% ^
    --conn_type=desktop ^
    --network_type=webrtc_direct ^
    --stream_name=V2ViUklDIExvY2FsIFRlc3Q= ^
    --device_id=%VISITOR_DEVICE_ID% ^
    --device_rp= ^
    --device_sp= ^
    --remote_device_id=%REMOTE_DEVICE_ID% ^
    --remote_device_rp=%RP_B64% ^
    --remote_device_sp= ^
    --enable_p2p=0 ^
    --auto_layout_screens=0 ^
    --display_name=My Computer ^
    --display_remote_name=%REMOTE_DEVICE_ID% ^
    --panel_server_port=29371 ^
    --screen_recording_path=. ^
    --my_host=127.0.0.1 ^
    --language=0 ^
    --only_viewing=false ^
    --split_windows=%SPLIT_WINDOWS% ^
    --max_num_of_screen=4 ^
    --display_logo=0 ^
    --develop_mode=1 ^
    --titlebar_color=-1 ^
    --decoder=auto ^
    --relay_host=127.0.0.1 ^
    --relay_port=0 ^
    --relay_appkey=test_appkey ^
    --force_software=0 ^
    --wait_debug=0 ^
    --force_gdi_capture=0 ^
    --disable_vulkan_render=1 ^
    --show_watermark=0 ^
    --gl_backend=opengl ^
    --force_direct=0

endlocal
