@echo off
setlocal enabledelayedexpansion

rem ============================================================================
rem build_px_auth_server.bat
rem
rem Build and deploy px_auth_server in one step:
rem   [1/3] Build the Vue frontend   web\px_auth         (npm run build)
rem   [2/3] Build the Rust server    px_auth_server      (cargo build --release)
rem   [3/3] Copy artifacts into      output\px_auth_server\
rem           exe      -> output\px_auth_server\px_auth.exe
rem           frontend -> output\px_auth_server\web_auth\
rem           (the server serves static files from the web_auth\ dir next to the exe)
rem
rem Notes:
rem   - Existing *.toml configs, certs\ and runtime data under output\ are
rem     NOT touched.
rem   - For the first full deployment (certs, license keys, config template)
rem     use scripts\package_px_auth_server.bat instead.
rem   - A running server locks its exe; stop it first, otherwise the exe copy
rem     step fails with an explicit error.
rem ============================================================================

cd /d "%~dp0"
set "REPO_ROOT=%cd%"

rem --- Per-server settings ---
set "SERVER_NAME=px_auth_server"
set "EXE_NAME=px_auth"
set "WEB_SRC=%REPO_ROOT%\web\px_auth"
rem Subdirectory under output\%SERVER_NAME%\ that holds the frontend files.
set "WEB_SUBDIR=web_auth"
set "OUTPUT_DIR=%REPO_ROOT%\output\%SERVER_NAME%"

rem aws-lc-sys needs a real NASM. Its fallback prebuilt-nasm shim script
rem breaks cmake configure under Git Bash (the .sh variant gets selected,
rem which the Ninja generator cannot identify), so use the repo-vendored
rem NASM instead of relying on whatever happens to be on the machine.
set "PATH=%REPO_ROOT%\tools\nasm;%PATH%"

rem protocol build.rs needs protoc; VsDevCmd can point VCPKG_ROOT at the
rem VS-bundled vcpkg (no protobuf installed), so default to the vendored one.
if not defined PROTOC set "PROTOC=%REPO_ROOT%\tools\protoc.exe"

echo ============================================
echo Building %SERVER_NAME%
echo ============================================
echo.

rem --- Check prerequisites ---
call :check_tool npm
if errorlevel 1 exit /b 1
call :check_tool cargo
if errorlevel 1 exit /b 1
rem NASM is required by aws-lc-sys (vendored under tools\nasm, PATH set above).
rem Ninja is checked later, after the VS2026 environment is activated -
rem VS bundles its own ninja, which is not on PATH before that.
call :check_tool nasm
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

rem --- Build environment: VS2026 (VS 18) + Ninja -------------------------
rem This project builds the Rust servers with VS2026 only. If the caller has
rem not already activated a VS 18 environment (e.g. build_official.bat),
rem locate VS2026 and activate it here.
if not "%VisualStudioVersion:~0,2%"=="18" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    set "VS_INSTALL_DIR="
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -version "[18.0,19.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL_DIR=%%i"
    )
    if "!VS_INSTALL_DIR!"=="" (
        if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Community"
        if exist "%ProgramFiles%\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Professional"
        if exist "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise"
        if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\BuildTools"
    )
    if "!VS_INSTALL_DIR!"=="" (
        echo ERROR: Visual Studio 2026 ^(VS 18^) with MSVC x64 tools was not found.
        echo        The Rust servers require VS2026 to build.
        exit /b 1
    )
    echo Activating VS2026 build environment: !VS_INSTALL_DIR!
    call "!VS_INSTALL_DIR!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
    if errorlevel 1 exit /b 1
)

rem Ninja is required (CMAKE_GENERATOR=Ninja is forced below). Checked only
rem now because VsDevCmd puts the VS-bundled ninja on PATH.
call :check_tool ninja
if errorlevel 1 exit /b 1

rem cmake-rs (used by aws-lc-sys) does not recognize VS 18 and panics with
rem "unsupported or unknown VisualStudio version: 18.0", so force the Ninja
rem generator to make cmake-rs skip VS detection. This must be UNCONDITIONAL:
rem aws-lc-sys caches its cmake build dir under target\, and alternating
rem generators (Ninja vs "Visual Studio 17 2022") between runs makes cmake
rem fail with "generator ... does not match the generator used previously".
set "CMAKE_GENERATOR=Ninja"

rem aws-lc-sys generates err_data.c via `go run`. A globally-set GOOS=linux
rem makes go cross-compile Linux binaries that cannot run on Windows
rem ("executable file not found in %PATH%"), so pin GOOS for this build.
set "GOOS=windows"

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
    echo       NOTE: certs\ and %EXE_NAME%.toml are still required;
    echo       run scripts\package_%SERVER_NAME%.bat once for a full deployment.
    mkdir "%OUTPUT_DIR%"
)

rem exe (fails while the server is running and locking the file)
copy /Y "%REPO_ROOT%\rust_server\target\release\%EXE_NAME%.exe" "%OUTPUT_DIR%\%EXE_NAME%.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy %EXE_NAME%.exe.
    echo        The server is probably still running and locking the exe.
    echo        Stop it, then re-run this script.
    exit /b 1
)

rem config template (seed on first deploy only; never overwrite an existing config)
if not exist "%OUTPUT_DIR%\%EXE_NAME%.toml" (
    copy /Y "%REPO_ROOT%\rust_server\target\release\%EXE_NAME%.toml" "%OUTPUT_DIR%\%EXE_NAME%.toml" >nul
    if errorlevel 1 echo WARNING: Failed to copy %EXE_NAME%.toml.
)
rem shared TLS cert (all servers share one; seed on first deploy only)
if not exist "%OUTPUT_DIR%\certs\cert.pem" (
    call "%REPO_ROOT%\scripts\ensure_tls_cert.bat" "%OUTPUT_DIR%\certs"
    if errorlevel 1 (
        echo ERROR: Failed to ensure TLS certificate for %SERVER_NAME%.
        exit /b 1
    )
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
