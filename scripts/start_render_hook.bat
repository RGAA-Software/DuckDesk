@echo off
setlocal enabledelayedexpansion

rem Game-hook launcher: start GammaRayRender only (no browser / no CDP).
rem This file is a parameter reference — pass mode/capture/game-path via CLI.

set "REPO_ROOT=%~dp0.."
cd /d "%REPO_ROOT%"

set "DIST=%REPO_ROOT%\build_official\dist"
set "SRC_TOML=%REPO_ROOT%\src\gr_render\settings.toml"

rem ===== launch parameters =====
set "PORT=32000"
set "DEVICE_ID=debug1"
set "APP_MODE=game-hook"
set "GAME_PATH=D:\1_test_games\CarGame\CarGame\Binaries\Win64\VehicleGame-Win64-Shipping.exe"
set "CAPTURE_VIDEO=true"
set "CAPTURE_VIDEO_TYPE=inner"
set "CAPTURE_AUDIO=true"
set "CAPTURE_AUDIO_TYPE=global"
set "WEBRTC_ENABLED=true"
set "WEBSOCKET_ENABLED=true"
set "ENCODER_FPS=60"
set "ENCODER_BITRATE=20"
set "ENCODER_FORMAT=h264"
set "WEB_URL=http://127.0.0.1:%PORT%/web_client/?deviceId=%DEVICE_ID%"

if not exist "%DIST%\GammaRayRender.exe" (
    echo ERROR: %DIST%\GammaRayRender.exe not found.
    exit /b 1
)
if not exist "%SRC_TOML%" (
    echo ERROR: %SRC_TOML% not found.
    exit /b 1
)

echo ============================================
echo Game-hook: start GammaRayRender only
echo ============================================
echo Dist : %DIST%
echo URL  : %WEB_URL%
echo.
echo Args:
echo   --app_mode=%APP_MODE%
echo   --app_game_path=%GAME_PATH%
echo   --capture_video=%CAPTURE_VIDEO%
echo   --capture_video_type=%CAPTURE_VIDEO_TYPE%
echo   --capture_audio=%CAPTURE_AUDIO%
echo   --capture_audio_type=%CAPTURE_AUDIO_TYPE%
echo   --webrtc_enabled=%WEBRTC_ENABLED%
echo   --websocket_enabled=%WEBSOCKET_ENABLED%
echo   --encoder_fps=%ENCODER_FPS%
echo   --encoder_bitrate=%ENCODER_BITRATE%
echo   --encoder_format=%ENCODER_FORMAT%
echo   --network_listen_port=%PORT%
echo   --logfile
echo.

taskkill /F /IM GammaRayRender.exe >nul 2>nul
timeout /t 1 /nobreak >nul

copy /Y "%SRC_TOML%" "%DIST%\settings.toml" >nul
if errorlevel 1 (
    echo ERROR: failed to copy settings.toml into dist.
    exit /b 1
)

echo Starting GammaRayRender.exe ...
powershell -NoProfile -Command "Start-Process -FilePath '%DIST%\GammaRayRender.exe' -ArgumentList '--logfile','--app_mode=%APP_MODE%','--app_game_path=%GAME_PATH%','--capture_video=%CAPTURE_VIDEO%','--capture_video_type=%CAPTURE_VIDEO_TYPE%','--capture_audio=%CAPTURE_AUDIO%','--capture_audio_type=%CAPTURE_AUDIO_TYPE%','--webrtc_enabled=%WEBRTC_ENABLED%','--websocket_enabled=%WEBSOCKET_ENABLED%','--encoder_fps=%ENCODER_FPS%','--encoder_bitrate=%ENCODER_BITRATE%','--encoder_format=%ENCODER_FORMAT%','--network_listen_port=%PORT%' -WorkingDirectory '%DIST%' -WindowStyle Normal"
if errorlevel 1 (
    echo ERROR: failed to Start-Process GammaRayRender.exe
    exit /b 1
)

echo Waiting for HTTP on port %PORT% ...
set /a _tries=0
:wait_port
set /a _tries+=1
powershell -NoProfile -Command "try { $r = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:%PORT%/api/ping' -TimeoutSec 1; if ($r.StatusCode -ge 200) { exit 0 } else { exit 1 } } catch { exit 1 }"
if not errorlevel 1 goto :port_ready
if !_tries! GEQ 60 (
    echo ERROR: render not ready on port %PORT% within 60s.
    echo Log: C:\Users\Public\GoDesk\gr_logs\godesk_render_%PORT%.log
    exit /b 1
)
timeout /t 1 /nobreak >nul
goto :wait_port

:port_ready
echo.
echo Render is up. Open this URL yourself:
echo   %WEB_URL%
echo Log: C:\Users\Public\GoDesk\gr_logs\godesk_render_%PORT%.log
echo.
endlocal
exit /b 0
