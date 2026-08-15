@echo off
setlocal enabledelayedexpansion

:: Package px_auth_server for standalone deployment.
:: 1. Ensure shared TLS certificate (generated once, reused across servers).
:: 1b. Generate Ed25519 license signing key pair (if missing).
:: 2. Build the Vue auth frontend.
:: 3. Build the Rust auth server.
:: 4. Copy exe + web assets + config + certs into output\px_auth_server\.
::
:: Result: run output\px_auth_server\px_auth_server.exe and open
::         https://localhost:30400 in a browser.

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"
set "OUTPUT_DIR=%REPO_ROOT%\output\px_auth_server"
set "CERT_DIR=%OUTPUT_DIR%\certs"
set "WEB_SRC=%REPO_ROOT%\web\gr_auth"
set "SERVER_SRC=%REPO_ROOT%\rust_server\px_auth_server"
set "SERVER_WORKSPACE=%REPO_ROOT%\rust_server"

echo ============================================
echo Packaging px_auth_server
echo ============================================
echo.

:: --- Check prerequisites ---
call :check_tool cargo
if errorlevel 1 exit /b 1
call :check_tool npm
if errorlevel 1 exit /b 1

:: --- Create output directory ---
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

:: --- 1. Ensure shared TLS certificate and copy to output ---
echo [1/4] Ensuring TLS certificate...
call "%~dp0\ensure_tls_cert.bat" "%CERT_DIR%"
if errorlevel 1 exit /b 1
echo.

:: --- 1b. Generate Ed25519 license signing key pair if missing ---
if exist "%CERT_DIR%\auth_license_private.key" (
    if exist "%CERT_DIR%\auth_license_public.key" (
        echo [1b/4] License signing key pair already exists, skipping generation.
        goto :license_keys_done
    )
)

echo [1b/4] Generating Ed25519 license signing key pair (PKCS#8 v1)...
%OPENSSL_EXE% genpkey -algorithm ED25519 -outform DER -out "%CERT_DIR%\auth_license_private.der"
if errorlevel 1 (
    echo ERROR: Failed to generate license private key.
    exit /b 1
)
%OPENSSL_EXE% pkey -in "%CERT_DIR%\auth_license_private.der" -inform DER -pubout -outform DER -out "%CERT_DIR%\auth_license_public.der"
if errorlevel 1 (
    echo ERROR: Failed to derive license public key.
    exit /b 1
)
%OPENSSL_EXE% base64 -in "%CERT_DIR%\auth_license_private.der" -out "%CERT_DIR%\auth_license_private.key"
%OPENSSL_EXE% base64 -in "%CERT_DIR%\auth_license_public.der" -out "%CERT_DIR%\auth_license_public.key"
del "%CERT_DIR%\auth_license_private.der" >nul 2>nul
del "%CERT_DIR%\auth_license_public.der" >nul 2>nul
echo       Private key: %CERT_DIR%\auth_license_private.key
echo       Public key : %CERT_DIR%\auth_license_public.key

:license_keys_done
echo.

:: --- 2. Build auth frontend ---
echo [2/4] Building auth frontend...
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

:: --- 3. Build auth server ---
echo [3/4] Building auth server...
cd /d "%SERVER_WORKSPACE%"
python "%SERVER_WORKSPACE%\set_server_version.py" px_auth_server --bump
if errorlevel 1 (
    echo ERROR: version bump failed.
    exit /b 1
)
cargo build -p px_auth_server --release
if errorlevel 1 (
    echo ERROR: cargo build failed.
    exit /b 1
)
echo.

:: --- 4. Copy artifacts to output directory ---
echo [4/4] Copying artifacts to %OUTPUT_DIR%...

:: exe
copy /Y "%SERVER_WORKSPACE%\target\release\px_auth_server.exe" "%OUTPUT_DIR%\px_auth_server.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy px_auth_server.exe.
    exit /b 1
)

:: config
copy /Y "%SERVER_SRC%\src\gr_auth_server_settings.toml" "%OUTPUT_DIR%\gr_auth_server_settings.toml" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy gr_auth_server_settings.toml.
    exit /b 1
)

:: web assets
if exist "%OUTPUT_DIR%\web_auth" rmdir /S /Q "%OUTPUT_DIR%\web_auth"
mkdir "%OUTPUT_DIR%\web_auth"
xcopy /E /I /Y "%WEB_SRC%\dist\*" "%OUTPUT_DIR%\web_auth\" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy web assets.
    exit /b 1
)

echo.
echo ============================================
echo Packaging complete!
echo ============================================
echo Output: %OUTPUT_DIR%
echo.
echo Before running:
echo   1. Start MongoDB (default: mongodb://localhost:27017/)
echo   2. Edit %OUTPUT_DIR%\gr_auth_server_settings.toml
echo      - bootstrap.admin_password: set initial admin password
echo      (jwt_secret is auto-generated on every startup)
echo   3. Distribute %OUTPUT_DIR%\certs\auth_license_public.key to CMS servers.
echo.
echo Run:
echo   %OUTPUT_DIR%\px_auth_server.exe
echo.
echo Open in browser:
echo   https://localhost:30400
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
