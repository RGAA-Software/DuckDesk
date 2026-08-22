@echo off
setlocal

rem Launch the standalone file manager against a LAN Render WebSocket endpoint.
rem Usage: scripts\test_file_transfer_only_wss.bat remote_device_id host port ticket_b64 nonce
rem Obtain the one-time file ticket from CMS immediately before running this script.

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
set "HOST=%~2"
if "%HOST%"=="" set "HOST=127.0.0.1"
set "PORT=%~3"
if "%PORT%"=="" set "PORT=20371"
set "TICKET_B64=%~4"
set "NONCE=%~5"
if "%REMOTE_DEVICE_ID%"=="" (
    echo ERROR: remote_device_id is required.
    exit /b 2
)
if "%TICKET_B64%"=="" (
    echo ERROR: ticket_b64 is required; standalone file transfer rejects password-only launches.
    exit /b 2
)
if "%NONCE%"=="" (
    echo ERROR: nonce is required.
    exit /b 2
)

set "VISITOR_DEVICE_ID=900000001"
set "STREAM_ID=file_only_wss_test"
echo Starting authenticated file-only WS test: %HOST%:%PORT% ^> %REMOTE_DEVICE_ID%

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
    --connection_ticket=%TICKET_B64% ^
    --connection_nonce=%NONCE% ^
    --enable_p2p=0 ^
    --display_name=My%20Computer ^
    --display_remote_name=%REMOTE_DEVICE_ID% ^
    --panel_server_port=29371 ^
    --screen_recording_path=. ^
    --my_host=127.0.0.1 ^
    --language=0 ^
    --disable_vulkan_render=1

endlocal
