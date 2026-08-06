@echo off
setlocal enabledelayedexpansion

rem Only start GammaRayRender (no browser / no CDP). Open the URL yourself.

set "REPO_ROOT=%~dp0.."
cd /d "%REPO_ROOT%"

set "DIST=%REPO_ROOT%\build_official\dist"
set "SRC_TOML=%REPO_ROOT%\src\gr_render\settings.toml"
set "PORT=32000"
set "DEVICE_ID=debug1"
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
echo Start GammaRayRender only
echo ============================================
echo Dist : %DIST%
echo URL  : %WEB_URL%
echo.

taskkill /F /IM GammaRayRender.exe >nul 2>nul
timeout /t 1 /nobreak >nul

copy /Y "%SRC_TOML%" "%DIST%\settings.toml" >nul
if errorlevel 1 (
    echo ERROR: failed to copy settings.toml into dist.
    exit /b 1
)

echo Starting GammaRayRender.exe --isolate --network_listen_port=%PORT% ...
powershell -NoProfile -Command "Start-Process -FilePath '%DIST%\GammaRayRender.exe' -ArgumentList '--isolate','--logfile','--network_listen_port=%PORT%' -WorkingDirectory '%DIST%' -WindowStyle Normal"
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
