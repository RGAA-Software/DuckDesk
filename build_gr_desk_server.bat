@echo off
setlocal enabledelayedexpansion

rem ============================================================================
rem build_gr_desk_server.bat
rem
rem Build and deploy gr_desk_server in one step:
rem   [1/3] Build the Vue frontend   web\gr_desk         (npm run build)
rem   [2/3] Build the Rust server    gr_desk_server      (cargo build --release)
rem   [3/3] Copy artifacts into      output\gr_desk_server\
rem           exe      -> output\gr_desk_server\gr_desk_server.exe
rem           frontend -> output\gr_desk_server\static\
rem           (the server serves static files from the static\ dir next to the exe)
rem
rem Notes:
rem   - Existing *.toml configs, certs\ and runtime data under output\ are
rem     NOT touched.
rem   - For the first full deployment (certs, config template)
rem     use scripts\package_gr_desk_server.bat instead.
rem   - A running server locks its exe; stop it first, otherwise the exe copy
rem     step fails with an explicit error.
rem ============================================================================

cd /d "%~dp0"
set "REPO_ROOT=%cd%"

rem --- Per-server settings ---
set "SERVER_NAME=gr_desk_server"
set "WEB_SRC=%REPO_ROOT%\web\gr_desk"
rem Subdirectory under output\%SERVER_NAME%\ that holds the frontend files.
set "WEB_SUBDIR=static"
set "OUTPUT_DIR=%REPO_ROOT%\output\%SERVER_NAME%"

echo ============================================
echo Building %SERVER_NAME%
echo ============================================
echo.

rem --- Check prerequisites ---
call :check_tool npm
if errorlevel 1 exit /b 1
call :check_tool cargo
if errorlevel 1 exit /b 1

rem --- [1/3] Build the frontend ---
echo [1/3] Building frontend: %WEB_SRC%
cd /d "%WEB_SRC%"
if not exist "node_modules" (
    echo npm install...
    call npm install
    if errorlevel 1 (
        echo ERROR: npm install failed in %WEB_SRC%.
        exit /b 1
    )
)
call npm run build
if errorlevel 1 (
    echo ERROR: npm run build failed in %WEB_SRC%.
    exit /b 1
)
if not exist "dist\index.html" (
    echo ERROR: frontend build did not produce dist\index.html.
    exit /b 1
)
echo.

rem --- [2/3] Build the Rust server ---
echo [2/3] Building Rust server: %SERVER_NAME% ^(release^)
cd /d "%REPO_ROOT%\rust_server"
rem cmake-rs (used by aws-lc-sys) does not recognize VS 18 and panics with
rem "unsupported or unknown VisualStudio version: 18.0". When the caller
rem (build_official.bat) activated a VS 18 environment, force the Ninja
rem generator so cmake-rs skips VS detection. Standalone runs keep the default.
if "%VisualStudioVersion:~0,2%"=="18" set "CMAKE_GENERATOR=Ninja"
cargo build --release -p %SERVER_NAME%
if errorlevel 1 (
    echo ERROR: cargo build failed for %SERVER_NAME%.
    exit /b 1
)
echo.

rem --- [3/3] Copy artifacts to output\ ---
echo [3/3] Copying artifacts to %OUTPUT_DIR%
if not exist "%OUTPUT_DIR%" (
    echo       output dir does not exist yet, creating it.
    echo       NOTE: certs\ and %SERVER_NAME%_settings.toml are still required;
    echo       run scripts\package_%SERVER_NAME%.bat once for a full deployment.
    mkdir "%OUTPUT_DIR%"
)

rem exe (fails while the server is running and locking the file)
copy /Y "%REPO_ROOT%\rust_server\target\release\%SERVER_NAME%.exe" "%OUTPUT_DIR%\%SERVER_NAME%.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy %SERVER_NAME%.exe.
    echo        The server is probably still running and locking the exe.
    echo        Stop it, then re-run this script.
    exit /b 1
)

rem frontend (wipe target first so stale hashed assets cannot linger)
if exist "%OUTPUT_DIR%\%WEB_SUBDIR%" rmdir /S /Q "%OUTPUT_DIR%\%WEB_SUBDIR%"
mkdir "%OUTPUT_DIR%\%WEB_SUBDIR%"
xcopy /E /I /Y "%WEB_SRC%\dist\*" "%OUTPUT_DIR%\%WEB_SUBDIR%\" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy frontend files to %OUTPUT_DIR%\%WEB_SUBDIR%.
    exit /b 1
)

echo.
echo ============================================
echo %SERVER_NAME% build + deploy complete!
echo ============================================
echo Output: %OUTPUT_DIR%
echo If the server was already running, restart it to pick up the new exe.
echo.

endlocal
exit /b 0

rem --- Helper: check that a tool is in PATH ---
:check_tool
where %~1 >nul 2>nul
if errorlevel 1 (
    echo ERROR: Required tool '%~1' is not found in PATH.
    echo        Please install Node.js / npm and Rust / cargo first.
    exit /b 1
)
exit /b 0
