@echo off
setlocal enabledelayedexpansion

rem ============================================================================
rem build_cms_web.bat
rem
rem Build and deploy ONLY the CMS web frontend (web\gr_cms). Reusable after
rem every frontend-only change, without rebuilding the Rust server or anything
rem else.
rem
rem   [1/2] Build the Vue frontend   web\gr_cms      (npm ci -> npm run build)
rem   [2/2] Deploy dist\* into       output\px_cms_server\web\
rem
rem Notes:
rem   - px_cms_server serves static files from the web\ dir next to its exe,
rem     so this is where the frontend must land.
rem   - The target web\ dir is wiped first so stale hashed vite assets cannot
rem     linger alongside the new ones.
rem   - This script does NOT rebuild px_cms_server.exe; if you also changed Rust
rem     code, run build_gr_cms_server.bat instead.
rem ============================================================================

cd /d "%~dp0"
set "REPO_ROOT=%cd%"

set "WEB_SRC=%REPO_ROOT%\web\gr_cms"
set "OUTPUT_DIR=%REPO_ROOT%\output\px_cms_server"
set "WEB_SUBDIR=web"

echo ============================================
echo Building CMS web frontend
echo ============================================
echo.

call :check_tool npm
if errorlevel 1 exit /b 1

rem --- [1/2] Build the frontend ---
echo [1/2] Building frontend: %WEB_SRC%
cd /d "%WEB_SRC%"

rem Always sync deps first so package.json additions cannot leave a stale
rem node_modules; fall back to npm install when npm ci cannot run.
echo npm ci...
call npm ci
if errorlevel 1 (
    echo npm ci failed, falling back to npm install...
    call npm install
    if errorlevel 1 (
        echo ERROR: npm install failed in %WEB_SRC%.
        exit /b 1
    )
)

echo npm run build...
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

rem --- [2/2] Deploy into output\px_cms_server\web\ ---
echo [2/2] Deploying to %OUTPUT_DIR%\%WEB_SUBDIR%
if not exist "%OUTPUT_DIR%" (
    echo       output dir does not exist yet, creating it.
    echo       NOTE: for a first-time full deployment - exe, certs, config -
    echo       run build_gr_cms_server.bat or scripts\package_gr_cms_server.bat.
    mkdir "%OUTPUT_DIR%"
)

if exist "%OUTPUT_DIR%\%WEB_SUBDIR%" rmdir /S /Q "%OUTPUT_DIR%\%WEB_SUBDIR%"
mkdir "%OUTPUT_DIR%\%WEB_SUBDIR%"
xcopy /E /I /Y "%WEB_SRC%\dist\*" "%OUTPUT_DIR%\%WEB_SUBDIR%\" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy frontend files to %OUTPUT_DIR%\%WEB_SUBDIR%.
    exit /b 1
)

echo.
echo ============================================
echo CMS web build + deploy complete!
echo ============================================
echo Output: %OUTPUT_DIR%\%WEB_SUBDIR%
echo If px_cms_server is running, restart it to serve the new assets.
echo.

endlocal
exit /b 0

rem --- Helper: check that a tool is in PATH ---
:check_tool
where %~1 >nul 2>nul
if errorlevel 1 (
    echo ERROR: Required tool '%~1' is not found in PATH.
    echo        Please install Node.js / npm first.
    exit /b 1
)
exit /b 0
