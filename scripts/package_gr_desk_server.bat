@echo off
setlocal enabledelayedexpansion

:: Package px_desk_server for standalone deployment.
:: 1. Ensure shared TLS certificate (generated once, reused across servers).
:: 2. Build the Vue desk frontend.
:: 3. Build the Rust desk server.
:: 4. Copy exe + web assets + certs into output\px_desk_server\.
::
:: Result: run output\px_desk_server\px_desk_server.exe
::         HTTP  on http://localhost:5000
::         HTTPS on https://localhost:5001

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"
set "OUTPUT_DIR=%REPO_ROOT%\output\px_desk_server"
set "CERT_DIR=%OUTPUT_DIR%\certs"
set "WEB_SRC=%REPO_ROOT%\web\gr_desk"
set "SERVER_SRC=%REPO_ROOT%\rust_server\px_desk_server"
set "SERVER_WORKSPACE=%REPO_ROOT%\rust_server"

echo ============================================
echo Packaging px_desk_server
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

:: --- 2. Build desk frontend ---
echo [2/4] Building desk frontend...
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

:: --- 3. Build desk server ---
echo [3/4] Building desk server...
cd /d "%SERVER_WORKSPACE%"
python "%SERVER_WORKSPACE%\set_server_version.py" px_desk_server --bump
if errorlevel 1 (
    echo ERROR: version bump failed.
    exit /b 1
)
cargo build -p px_desk_server --release
if errorlevel 1 (
    echo ERROR: cargo build failed.
    exit /b 1
)
echo.

:: --- 4. Copy artifacts to output directory ---
echo [4/4] Copying artifacts to %OUTPUT_DIR%...

:: exe
copy /Y "%SERVER_WORKSPACE%\target\release\px_desk_server.exe" "%OUTPUT_DIR%\px_desk_server.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy px_desk_server.exe.
    exit /b 1
)

:: web assets (served from <exe_dir>/static)
if exist "%OUTPUT_DIR%\static" rmdir /S /Q "%OUTPUT_DIR%\static"
mkdir "%OUTPUT_DIR%\static"
xcopy /E /I /Y "%WEB_SRC%\dist\*" "%OUTPUT_DIR%\static\" >nul
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
echo.
echo Run:
echo   %OUTPUT_DIR%\px_desk_server.exe
echo.
echo Open in browser:
echo   http://localhost:5000
echo   https://localhost:5001
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
