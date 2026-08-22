@echo off
setlocal

rem Launch the standalone file manager against a LAN Render WebSocket endpoint.
rem Usage: scripts\test_file_transfer_only_wss.bat [remote_device_id] [random_pwd] [host] [port]
rem The Render must already be running and file transfer must be enabled.

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"
set "CLIENT_EXE=%REPO_ROOT%\build_official\dist\px_client.exe"
if not exist "%CLIENT_EXE%" (
    echo ERROR: %CLIENT_EXE% not found. Run scripts\build_px_client.bat first.
    exit /b 1
)
if not exist "%REPO_ROOT%\build_official\dist\deps\ct_plugins\ft_client.dll" (
    echo ERROR: ft_client.dll is missing. Run scripts\build_px_client.bat first.
    exit /b 1
)

set "REMOTE_DEVICE_ID=%~1"
if "%REMOTE_DEVICE_ID%"=="" set "REMOTE_DEVICE_ID=600378210"
set "RANDOM_PWD=%~2"
set "HOST=%~3"
if "%HOST%"=="" set "HOST=127.0.0.1"
set "PORT=%~4"
if "%PORT%"=="" set "PORT=20371"
set "RP_B64="
if not "%RANDOM_PWD%"=="" (
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('%RANDOM_PWD%'))"`) do set "RP_B64=%%i"
)

set "VISITOR_DEVICE_ID=900000001"
set "STREAM_ID=file_only_wss_test"
echo Starting file-only WSS test: %HOST%:%PORT% ^> %REMOTE_DEVICE_ID%

"%CLIENT_EXE%" ^
    --mode=file-transfer ^
    --host=%HOST% ^
    --port=%PORT% ^
    --appkey=test_appkey ^
    --cms_host=127.0.0.1 ^
    --cms_port=30500 ^
    --stream_id=%STREAM_ID% ^
    --network_type=websocket ^
    --stream_name=RmlsZSBUcmFuc2ZlciBXU1M= ^
    --device_id=%VISITOR_DEVICE_ID% ^
    --remote_device_id=%REMOTE_DEVICE_ID% ^
    --remote_device_rp=%RP_B64% ^
    --enable_p2p=0 ^
    --display_name=My%20Computer ^
    --display_remote_name=%REMOTE_DEVICE_ID% ^
    --panel_server_port=29371 ^
    --screen_recording_path=. ^
    --my_host=127.0.0.1 ^
    --language=0 ^
    --disable_vulkan_render=1

endlocal
