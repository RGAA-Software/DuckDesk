@echo off
setlocal enabledelayedexpansion

rem Standalone game-hook debug launcher.
rem Starts ONLY px_render (no panel / px_service), then opens web client.
rem See docs/game_hook_capture_plan.md

set "REPO_ROOT=%~dp0.."
cd /d "%REPO_ROOT%"

set "DIST=%REPO_ROOT%\build_official\dist"
set "SRC_TOML=%REPO_ROOT%\src\px_render\settings.toml"

rem ===== launch parameters =====
set "PORT=32000"
set "DEVICE_ID=debug1"
set "APP_MODE=game-hook"
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

rem Optional: UE bootstrap launcher → real game exe. Set GAME_VIEW_B64 to the
rem Base64(UTF-8) of the real renderer exe full path (service resolves this via
rem ue_bootstrap in the CMS flow); render then injects the view process instead
rem of the launcher shell.
set "VIEW_ARG="
if defined GAME_VIEW_B64 set "VIEW_ARG=--app_game_view_path=%GAME_VIEW_B64%"

if not exist "%DIST%\px_render.exe" (
    echo ERROR: %DIST%\px_render.exe not found.
    echo Run build_official.bat first.
    exit /b 1
)
if not exist "%DIST%\px_gh.dll" (
    echo ERROR: %DIST%\px_gh.dll not found.
    exit /b 1
)
if not exist "%DIST%\px_gh_injector.exe" (
    echo ERROR: %DIST%\px_gh_injector.exe not found.
    exit /b 1
)
if not exist "%SRC_TOML%" (
    echo ERROR: %SRC_TOML% not found.
    exit /b 1
)

echo ============================================
echo Game-hook render debug launch
echo ============================================
echo Dist : %DIST%
echo Toml : %SRC_TOML%
echo URL  : %WEB_URL%
echo.

rem Stop previous render on this port (best-effort).
taskkill /F /IM px_render.exe >nul 2>nul
timeout /t 1 /nobreak >nul

copy /Y "%SRC_TOML%" "%DIST%\settings.toml" >nul
if errorlevel 1 (
    echo ERROR: failed to copy settings.toml into dist.
    exit /b 1
)

echo.
echo Args: --app_mode=%APP_MODE% --capture_video_type=%CAPTURE_VIDEO_TYPE% --network_listen_port=%PORT%
echo game-path: from settings.toml
findstr /I /C:"game-path" "%DIST%\settings.toml"

echo.
echo Starting px_render.exe ...
rem Use PowerShell Start-Process so the render breaks away from the parent job
rem object (cmd "start" children get killed when Cursor/agent shells exit).
powershell -NoProfile -Command "Start-Process -FilePath '%DIST%\px_render.exe' -ArgumentList '--logfile','--app_mode=%APP_MODE%','--capture_video=%CAPTURE_VIDEO%','--capture_video_type=%CAPTURE_VIDEO_TYPE%','--capture_audio=%CAPTURE_AUDIO%','--capture_audio_type=%CAPTURE_AUDIO_TYPE%','--webrtc_enabled=%WEBRTC_ENABLED%','--websocket_enabled=%WEBSOCKET_ENABLED%','--encoder_fps=%ENCODER_FPS%','--encoder_bitrate=%ENCODER_BITRATE%','--encoder_format=%ENCODER_FORMAT%','--network_listen_port=%PORT%','%VIEW_ARG%' -WorkingDirectory '%DIST%' -WindowStyle Normal"
if errorlevel 1 (
    echo ERROR: failed to Start-Process px_render.exe
    exit /b 1
)

echo Waiting for render HTTP on port %PORT% ...
set /a _tries=0
:wait_port
set /a _tries+=1
powershell -NoProfile -Command "try { $r = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:%PORT%/api/ping' -TimeoutSec 1; if ($r.StatusCode -ge 200) { exit 0 } else { exit 1 } } catch { exit 1 }"
if not errorlevel 1 goto :port_ready
if !_tries! GEQ 60 (
    echo ERROR: render did not become ready on port %PORT% within 60s.
    echo Check log under ProgramData\Pixels\px_logs\pixels_render_%PORT%.log
    exit /b 1
)
timeout /t 1 /nobreak >nul
goto :wait_port

:port_ready
echo Render is up.

rem Optional: open visible browser with "browser" arg; default is headless CDP verify.
if /I "%1"=="browser" (
    echo Opening visible web client: %WEB_URL%
    start "" "%WEB_URL%"
    goto :done_msg
)

echo Waiting a few seconds for game start + inject...
ping -n 6 127.0.0.1 >nul
echo Running headless CDP video check...
where node >nul 2>&1
if errorlevel 1 (
    echo ERROR: node not in PATH; cannot run headless check.
    echo Manual URL: %WEB_URL%
    exit /b 1
)
node "%REPO_ROOT%\scripts\cdp_game_hook_video.mjs"
set "CDP_RC=%ERRORLEVEL%"
if not "%CDP_RC%"=="0" (
    echo Headless verify FAILED ^(exit %CDP_RC%^).
    echo Log: C:\Users\Public\Pixels\px_logs\pixels_render_%PORT%.log
    exit /b %CDP_RC%
)
echo Headless verify OK.

:done_msg
echo.
echo ============================================
echo Next checks
echo ============================================
echo 1. Game should launch from game-path in settings.toml
echo 2. Log should show: StartProcessWithHook / Inject success / IPC connected
echo 3. Default path uses headless CDP ^(no click^); use "browser" to open Chrome
echo 4. Log: C:\Users\Public\Pixels\px_logs\pixels_render_%PORT%.log
echo.
endlocal
exit /b 0
