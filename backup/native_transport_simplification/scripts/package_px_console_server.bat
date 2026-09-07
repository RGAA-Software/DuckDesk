@echo off
setlocal enabledelayedexpansion

:: Package px_console_server for standalone deployment.
:: 1. Ensure shared TLS certificate (generated once, reused across servers).
:: 2. Copy the Ed25519 license public key from a packaged px_auth_server (if present).
:: 3. Build the Vue Console frontend.
:: 4. Build the Rust Console server.
:: 5. Copy exe + web assets + config + certs into output\px_console\.
::
:: NOTE: No license file is shipped. The Console pulls its authorization from the
::       auth server at runtime (auth_server_url in px_console.toml,
::       see docs\px_console_auth_pull.md).
::
:: Result: run output\px_console\px_console.exe --running-mode=server
::         and open https://localhost:30500 in a browser.

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"
set "OUTPUT_DIR=%REPO_ROOT%\output\px_console"
set "LEGACY_OUTPUT_DIR=%REPO_ROOT%\output\px_cms"
set "CERT_DIR=%OUTPUT_DIR%\certs"
set "WEB_SRC=%REPO_ROOT%\web\px_console"
set "SERVER_SRC=%REPO_ROOT%\rust_server\px_console_server"
set "SERVER_WORKSPACE=%REPO_ROOT%\rust_server"
set "MEDIA_SRC=%SERVER_SRC%\media"
set "AUTH_SERVER_OUTPUT=%REPO_ROOT%\output\px_auth"

echo ============================================
echo Packaging px_console_server
echo ============================================
echo.

:: --- Check prerequisites ---
call :check_tool cargo
if errorlevel 1 exit /b 1
call :check_tool npm
if errorlevel 1 exit /b 1

:: --- Create output directory ---
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

:: Upgrade an existing Pixels CMS deployment without losing local state. The
:: copy is only performed when the corresponding Console destination is absent.
if exist "%LEGACY_OUTPUT_DIR%" (
    if not exist "%OUTPUT_DIR%\px_console.toml" if exist "%LEGACY_OUTPUT_DIR%\px_cms.toml" (
        copy /Y "%LEGACY_OUTPUT_DIR%\px_cms.toml" "%OUTPUT_DIR%\px_console.toml" >nul
        if errorlevel 1 exit /b 1
        echo Migrated legacy configuration to px_console.toml.
    )
    if not exist "%OUTPUT_DIR%\storage" if exist "%LEGACY_OUTPUT_DIR%\cms_storage" (
        robocopy "%LEGACY_OUTPUT_DIR%\cms_storage" "%OUTPUT_DIR%\storage" /E /NFL /NDL /NJH /NJS /NP >nul
        if errorlevel 8 exit /b 1
        echo Migrated legacy authorization cache.
    )
    if not exist "%OUTPUT_DIR%\uploads" if exist "%LEGACY_OUTPUT_DIR%\uploads" (
        robocopy "%LEGACY_OUTPUT_DIR%\uploads" "%OUTPUT_DIR%\uploads" /E /NFL /NDL /NJH /NJS /NP >nul
        if errorlevel 8 exit /b 1
        echo Migrated legacy uploads.
    )
    if not exist "%OUTPUT_DIR%\certs" if exist "%LEGACY_OUTPUT_DIR%\certs" (
        robocopy "%LEGACY_OUTPUT_DIR%\certs" "%OUTPUT_DIR%\certs" /E /NFL /NDL /NJH /NJS /NP >nul
        if errorlevel 8 exit /b 1
        echo Migrated legacy certificates.
    )
)

:: --- 1. Ensure shared TLS certificate and copy to output ---
echo [1/5] Ensuring TLS certificate...
call "%~dp0\ensure_tls_cert.bat" "%CERT_DIR%"
if errorlevel 1 exit /b 1
echo.

:: --- 2. Always refresh the Ed25519 public key from the packaged auth server.
:: The auth private key is the authority; retaining a stale Console public key
:: makes newly issued licenses fail signature verification.
if exist "%AUTH_SERVER_OUTPUT%\certs\auth_license_public.key" (
    echo [2/5] Refreshing license public key from packaged px_auth_server...
    copy /Y "%AUTH_SERVER_OUTPUT%\certs\auth_license_public.key" "%CERT_DIR%\auth_license_public.key" >nul
    if errorlevel 1 (
        echo ERROR: Failed to copy auth_license_public.key.
        exit /b 1
    )
    echo       Copied: %CERT_DIR%\auth_license_public.key
) else (
    echo [2/5] WARNING: auth_license_public.key not found.
    echo        Expected at: %AUTH_SERVER_OUTPUT%\certs\auth_license_public.key
    echo        Run scripts\package_px_auth_server.bat first, or obtain the
    echo        public key from your auth server and place it at:
    echo        %CERT_DIR%\auth_license_public.key
    echo        Alternatively set the PX_AUTH_LICENSE_PUBLIC_KEY env var.
)

echo.

:: --- 3. Build Console frontend ---
echo [3/5] Building Console frontend...
cd /d "%WEB_SRC%"
if not exist "node_modules" (
    call npm install
    if errorlevel 1 (
        echo ERROR: npm install failed.
        exit /b 1
    )
)
call npm run build
if errorlevel 1 (
    echo ERROR: npm run build failed.
    exit /b 1
)
echo.

:: --- 4. Build Console server ---
echo [4/5] Building Console server...
cd /d "%SERVER_WORKSPACE%"
python "%SERVER_WORKSPACE%\set_server_version.py" px_console_server --bump
if errorlevel 1 (
    echo ERROR: version bump failed.
    exit /b 1
)
cargo build -p px_console_server --release
if errorlevel 1 (
    echo ERROR: cargo build failed.
    exit /b 1
)
echo.

:: --- 5. Copy artifacts to output directory ---
echo [5/5] Copying artifacts to %OUTPUT_DIR%...

:: exe
copy /Y "%SERVER_WORKSPACE%\target\release\px_console.exe" "%OUTPUT_DIR%\px_console.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy px_console.exe.
    exit /b 1
)

:: config (preserve an existing or migrated deployment configuration)
if not exist "%OUTPUT_DIR%\px_console.toml" (
    copy /Y "%SERVER_SRC%\src\px_console.toml" "%OUTPUT_DIR%\px_console.toml" >nul
    if errorlevel 1 (
        echo ERROR: Failed to copy px_console.toml.
        exit /b 1
    )
)

:: Fixed media sidecars. ZLMediaKit requires its complete runtime because
:: FFmpeg/SRT/WebRTC/OpenSSL DLLs and media_www are loaded at runtime.
if not exist "%MEDIA_SRC%\px_media.exe" (
    echo ERROR: Fixed ZLMediaKit binary is missing: %MEDIA_SRC%\px_media.exe
    exit /b 1
)
if not exist "%MEDIA_SRC%\config.ini" (
    echo ERROR: ZLMediaKit config is missing: %MEDIA_SRC%\config.ini
    exit /b 1
)
if not exist "%MEDIA_SRC%\px_turn.exe" (
    echo ERROR: Bundled Coturn binary is missing: %MEDIA_SRC%\px_turn.exe
    echo        Run scripts\build_px_turn.bat first.
    exit /b 1
)
if not exist "%MEDIA_SRC%\turnserver.conf" (
    echo ERROR: Bundled Coturn config is missing: %MEDIA_SRC%\turnserver.conf
    exit /b 1
)
if not exist "%MEDIA_SRC%\COTURN_LICENSE" (
    echo ERROR: Bundled Coturn license is missing: %MEDIA_SRC%\COTURN_LICENSE
    exit /b 1
)
robocopy "%MEDIA_SRC%" "%OUTPUT_DIR%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo ERROR: Failed to copy the ZLMediaKit runtime.
    exit /b 1
)
if not exist "%OUTPUT_DIR%\px_media.exe" (
    echo ERROR: Failed to copy px_media.exe.
    exit /b 1
)
if not exist "%OUTPUT_DIR%\config.ini" (
    echo ERROR: Failed to copy ZLMediaKit config.ini.
    exit /b 1
)
if not exist "%OUTPUT_DIR%\px_turn.exe" (
    echo ERROR: Failed to copy px_turn.exe.
    exit /b 1
)
if not exist "%OUTPUT_DIR%\turnserver.conf" (
    echo ERROR: Failed to copy turnserver.conf.
    exit /b 1
)

:: web assets
if exist "%OUTPUT_DIR%\web" rmdir /S /Q "%OUTPUT_DIR%\web"
mkdir "%OUTPUT_DIR%\web"
xcopy /E /I /Y "%WEB_SRC%\dist\*" "%OUTPUT_DIR%\web\" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy web assets.
    exit /b 1
)

:: runtime directories
if not exist "%OUTPUT_DIR%\uploads" mkdir "%OUTPUT_DIR%\uploads"
if not exist "%OUTPUT_DIR%\uploads\logs" mkdir "%OUTPUT_DIR%\uploads\logs"
if not exist "%OUTPUT_DIR%\uploads\avatar" mkdir "%OUTPUT_DIR%\uploads\avatar"
if not exist "%OUTPUT_DIR%\uploads\update_info" mkdir "%OUTPUT_DIR%\uploads\update_info"

echo.
echo ============================================
echo Packaging complete!
echo ============================================
echo Output: %OUTPUT_DIR%
echo.
echo Before running:
echo   1. Start MongoDB (default: mongodb://localhost:27017/)
echo   2. Start Redis   (default: redis://127.0.0.1:6379/)
echo   3. Edit %OUTPUT_DIR%\px_console.toml
echo      - server_w3c_ip     : set this machine's public IP (or leave empty for auto)
echo      - mongodb_url       : set if MongoDB is not on localhost
echo      - redis_url         : set if Redis is not on localhost
echo      - auth_server_url   : address of the px_auth_server issuing licenses
echo      - [app_credential]  : same appkey/app_secret as the auth server (if required)
echo   4. Ensure %CERT_DIR%\auth_license_public.key is present
echo      (issued by px_auth_server).
echo   5. The Console auto-registers as a trial device on first run; switch it to
echo      licensed in the px_auth_server admin UI (Pixels Console device list).
echo.
echo Run (headless server mode):
echo   %OUTPUT_DIR%\px_console.exe --running-mode=server
echo.
echo Run (panel UI mode):
echo   %OUTPUT_DIR%\px_console.exe
echo.
echo Open in browser:
echo   https://localhost:30500
echo   Health check: https://localhost:30500/ping
echo.
echo NOTE: Since the certificate is self-signed, browsers will show a
echo       security warning. Accept the risk or replace certs\cert.pem
echo       and certs\key.pem with your own trusted certificate.
echo.

endlocal
exit /b 0

:: --- Helper: check that a tool is in PATH ---
:check_tool
where %~1 >nul 2>nul
if errorlevel 1 (
    echo ERROR: Required tool '%~1' is not found in PATH.
    echo.
    echo Please make sure the following are installed and in PATH:
    echo   - Rust / cargo
    echo   - Node.js / npm
    echo.
    pause
    exit /b 1
)
exit /b 0
