@echo off
setlocal enabledelayedexpansion

:: Package gr_auth_server for standalone deployment.
:: 1. Generate a 100-year self-signed TLS certificate (if missing).
:: 2. Build the Rust auth server.
:: 3. Build the Vue auth frontend.
:: 4. Copy exe + web assets + config + certs into output\gr_auth_server\.
::
:: Result: run output\gr_auth_server\gr_auth_server.exe and open
::         https://localhost:30400 in a browser.

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"
set "OUTPUT_DIR=%REPO_ROOT%\output\gr_auth_server"
set "CERT_DIR=%OUTPUT_DIR%\certs"
set "WEB_SRC=%REPO_ROOT%\web\gr_auth"
set "SERVER_SRC=%REPO_ROOT%\rust_server\gr_auth_server"
set "SERVER_WORKSPACE=%REPO_ROOT%\rust_server"

echo ============================================
echo Packaging gr_auth_server
echo ============================================
echo.

:: --- Check prerequisites ---
call :check_tool cargo
if errorlevel 1 exit /b 1
call :check_tool npm
if errorlevel 1 exit /b 1

:: --- Find or download OpenSSL ---
set "OPENSSL_EXE="
set "BUNDLED_OPENSSL=%REPO_ROOT%\tools\openssl\openssl.exe"

:: 1. Prefer system PATH
where openssl >nul 2>nul
if not errorlevel 1 (
    set "OPENSSL_EXE=openssl"
    goto :openssl_done
)

:: 2. Use previously downloaded bundle
if exist "%BUNDLED_OPENSSL%" (
    echo OpenSSL not found in PATH, using bundled: %BUNDLED_OPENSSL%
    set "OPENSSL_EXE=%BUNDLED_OPENSSL%"
    goto :openssl_done
)

:: 3. Download portable OpenSSL to tools\openssl\
echo OpenSSL not found in PATH. Downloading portable OpenSSL to tools\openssl...
set "OPENSSL_URL=https://download.firedaemon.com/FireDaemon-OpenSSL/openssl-3.5.7.zip"
set "OPENSSL_ZIP=%REPO_ROOT%\tools\temp\openssl.zip"
set "OPENSSL_TEMP=%REPO_ROOT%\tools\temp\openssl_extract"

if not exist "%REPO_ROOT%\tools\temp" mkdir "%REPO_ROOT%\tools\temp"
if not exist "%REPO_ROOT%\tools\openssl" mkdir "%REPO_ROOT%\tools\openssl"

curl -L -o "%OPENSSL_ZIP%" "%OPENSSL_URL%"
if errorlevel 1 (
    echo ERROR: Failed to download OpenSSL from %OPENSSL_URL%
    echo        Please check your network connection or install OpenSSL manually.
    pause
    exit /b 1
)

powershell -Command "Expand-Archive -Path '%OPENSSL_ZIP%' -DestinationPath '%OPENSSL_TEMP%' -Force"
if errorlevel 1 (
    echo ERROR: Failed to extract OpenSSL archive.
    pause
    exit /b 1
)

copy /Y "%OPENSSL_TEMP%\x64\bin\openssl.exe"       "%REPO_ROOT%\tools\openssl\" >nul
copy /Y "%OPENSSL_TEMP%\x64\bin\libcrypto-3-x64.dll" "%REPO_ROOT%\tools\openssl\" >nul
copy /Y "%OPENSSL_TEMP%\x64\bin\libssl-3-x64.dll"    "%REPO_ROOT%\tools\openssl\" >nul

rmdir /S /Q "%OPENSSL_TEMP%" >nul 2>nul
del "%OPENSSL_ZIP%" >nul 2>nul

if not exist "%BUNDLED_OPENSSL%" (
    echo ERROR: OpenSSL setup failed. openssl.exe was not extracted.
    pause
    exit /b 1
)

echo OpenSSL downloaded successfully.
set "OPENSSL_EXE=%BUNDLED_OPENSSL%"

:openssl_done

:: --- Create output directory ---
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if not exist "%CERT_DIR%" mkdir "%CERT_DIR%"

:: --- 1. Generate self-signed certificate (100 years) if missing ---
if exist "%CERT_DIR%\cert.pem" (
    if exist "%CERT_DIR%\key.pem" (
        echo [1/4] TLS certificate already exists, skipping generation.
        goto :cert_done
    )
)

echo [1/4] Generating 100-year self-signed TLS certificate...
(
    echo [req]
    echo distinguished_name = dn
    echo x509_extensions = v3_req
    echo prompt = no
    echo.
    echo [dn]
    echo CN = localhost
    echo.
    echo [v3_req]
    echo subjectAltName = @alt_names
    echo.
    echo [alt_names]
    echo DNS.1 = localhost
    echo IP.1 = 127.0.0.1
) > "%CERT_DIR%\cert.conf"
%OPENSSL_EXE% req -x509 -newkey rsa:4096 -keyout "%CERT_DIR%\key.pem" -out "%CERT_DIR%\cert.pem" -sha256 -days 36500 -nodes -config "%CERT_DIR%\cert.conf"
if errorlevel 1 (
    echo ERROR: Failed to generate TLS certificate.
    exit /b 1
)
del "%CERT_DIR%\cert.conf" >nul 2>nul
echo       Certificate: %CERT_DIR%\cert.pem
echo       Private key: %CERT_DIR%\key.pem

:cert_done
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
cargo build -p gr_auth_server --release
if errorlevel 1 (
    echo ERROR: cargo build failed.
    exit /b 1
)
echo.

:: --- 4. Copy artifacts to output directory ---
echo [4/4] Copying artifacts to %OUTPUT_DIR%...

:: exe
copy /Y "%SERVER_WORKSPACE%\target\release\gr_auth_server.exe" "%OUTPUT_DIR%\gr_auth_server.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy gr_auth_server.exe.
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
echo      - bootstrap.jwt_secret   : set a random string (>=32 chars)
echo      - bootstrap.admin_password: set initial admin password
echo.
echo Run:
echo   %OUTPUT_DIR%\gr_auth_server.exe
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
    echo   - OpenSSL
    echo.
    pause
    exit /b 1
)
exit /b 0
