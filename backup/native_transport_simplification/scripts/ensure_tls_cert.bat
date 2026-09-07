@echo off
setlocal enabledelayedexpansion

:: Ensures a shared self-signed TLS certificate exists and copies it to a target directory.
::
:: Usage: call ensure_tls_cert.bat <target_cert_dir>
::
:: The certificate is generated ONCE at <repo_root>\certs\ and reused by all
:: server packaging scripts. On success, OPENSSL_EXE is exported to the caller
:: so that scripts which need OpenSSL for other purposes (e.g. license key
:: generation) can reuse it.

set "TARGET_CERT_DIR=%~1"
if "%TARGET_CERT_DIR%"=="" (
    echo ERROR: ensure_tls_cert.bat requires target cert directory as argument.
    exit /b 1
)

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"
set "SHARED_CERT_DIR=%REPO_ROOT%\certs"

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

:: --- Create shared cert directory ---
if not exist "%SHARED_CERT_DIR%" mkdir "%SHARED_CERT_DIR%"

:: --- Generate shared self-signed certificate (100 years) if missing ---
if exist "%SHARED_CERT_DIR%\cert.pem" (
    if exist "%SHARED_CERT_DIR%\key.pem" (
        echo Shared TLS certificate already exists at %SHARED_CERT_DIR%, reusing.
        goto :copy_cert
    )
)

echo Generating 100-year self-signed TLS certificate at %SHARED_CERT_DIR%...
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
) > "%SHARED_CERT_DIR%\cert.conf"
%OPENSSL_EXE% req -x509 -newkey rsa:4096 -keyout "%SHARED_CERT_DIR%\key.pem" -out "%SHARED_CERT_DIR%\cert.pem" -sha256 -days 36500 -nodes -config "%SHARED_CERT_DIR%\cert.conf"
if errorlevel 1 (
    echo ERROR: Failed to generate TLS certificate.
    exit /b 1
)
del "%SHARED_CERT_DIR%\cert.conf" >nul 2>nul
echo       Certificate: %SHARED_CERT_DIR%\cert.pem
echo       Private key: %SHARED_CERT_DIR%\key.pem

:copy_cert

:: --- Copy to target directory ---
if not exist "%TARGET_CERT_DIR%" mkdir "%TARGET_CERT_DIR%"
copy /Y "%SHARED_CERT_DIR%\cert.pem" "%TARGET_CERT_DIR%\cert.pem" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy cert.pem to %TARGET_CERT_DIR%.
    exit /b 1
)
copy /Y "%SHARED_CERT_DIR%\key.pem" "%TARGET_CERT_DIR%\key.pem" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy key.pem to %TARGET_CERT_DIR%.
    exit /b 1
)
echo Copied TLS certificate to %TARGET_CERT_DIR%.

endlocal & set "OPENSSL_EXE=%OPENSSL_EXE%"
exit /b 0
