@echo off
setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL_DIR="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version "[18.0,19.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_DIR=%%i"
    )
)

if "%VS_INSTALL_DIR%"=="" (
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Community"
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Professional"
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise"
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\BuildTools"
)

if "%VS_INSTALL_DIR%"=="" (
    if exist "%VSWHERE%" (
        for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VS_INSTALL_DIR=%%i"
        )
    )
)

if "%VS_INSTALL_DIR%"=="" (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
)

if "%VS_INSTALL_DIR%"=="" (
    echo Failed to find Visual Studio 2022/2026 with MSVC x64 tools.
    exit /b 1
)

call "%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

rem VsDevCmd points VCPKG_ROOT to the VS-bundled vcpkg (no protobuf tools).
rem Pin protoc from the project vcpkg for rust_base/protocol build.rs.
if not defined PROTOC set "PROTOC=C:\source\vcpkg\installed\x64-windows-static-release\tools\protobuf\protoc.exe"

rem Auto-increment the product version on every build:
rem patch += 1; when patch would reach 100, minor += 1 and patch resets to 0.
python "%~dp0set_app_version.py" --bump
if errorlevel 1 exit /b %errorlevel%

for /f "delims=" %%a in ('dir /b /ad "%VS_INSTALL_DIR%\VC\Tools\MSVC" ^| "%SystemRoot%\System32\sort.exe" /r ^| findstr.exe /r "^[0-9]"') do (
    set "VC_TOOLS_DIR=%VS_INSTALL_DIR%\VC\Tools\MSVC\%%a\bin\Hostx64\x64"
    goto :found_vc
)
:found_vc
if not "%VC_TOOLS_DIR%"=="" (
    set "PATH=%VC_TOOLS_DIR%;%PATH%"
    for %%I in ("%VC_TOOLS_DIR%\..\..\..") do set "VC_MSVC_DIR=%%~fI"
    set "LIB=!VC_MSVC_DIR!\lib\x64;!VC_MSVC_DIR!\ATLMFC\lib\x64;%LIB%"
    set "INCLUDE=!VC_MSVC_DIR!\include;%INCLUDE%"
    echo Using VC tools: %VC_TOOLS_DIR%
    echo Using VC libs: !VC_MSVC_DIR!\lib\x64
)

if /I "%1"=="full" (
    echo Full build requested, running CMake configure...
    goto :do_configure
)
if /I "%1"=="reconfigure" (
    echo Reconfigure requested, running CMake configure...
    goto :do_configure
)
rem Default: incremental build (skip CMake configure).
rem Fall back to configure when the build tree does not exist yet.
if exist "build_official\build.ninja" (
    echo Incremental build, skipping CMake configure...
    goto :do_build
)
echo build_official not found, running CMake configure first...

:do_configure
cmake -S . -B build_official -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official -Wno-dev
if errorlevel 1 exit /b %errorlevel%

:do_build
echo ----------------------BUILD START------------------------
echo ---------------------------------------------------------
echo ---------------------------------------------------------

rem Sync message protos from the single source of truth (src\px_deps\px_message_new)
rem into the web clients so Vite can inline them. Generated at build time, not committed.
node "%~dp0scripts\sync_web_protos.mjs"
if errorlevel 1 exit /b %errorlevel%

rem Always rebuild the web client frontend so collect_dist packages fresh assets.
call :build_npm_web "web\px_web_client" "web_client"
if errorlevel 1 exit /b %errorlevel%

cmake --build build_official -j18
if errorlevel 1 exit /b %errorlevel%

rem Build and deploy the three rust servers (frontend + exe + copy to output\).
rem Each script is self-contained and can also be run standalone.
rem Skipped when PX_SKIP_SERVERS is set (see build_client.bat: installer only
rem needs client-side artifacts, servers are built separately).
if not defined PX_SKIP_SERVERS (
    call "%~dp0build_px_cms_server.bat"
    if errorlevel 1 exit /b %errorlevel%
    call "%~dp0build_px_auth_server.bat"
    if errorlevel 1 exit /b %errorlevel%
    call "%~dp0build_px_desk_server.bat"
    if errorlevel 1 exit /b %errorlevel%
) else (
    echo PX_SKIP_SERVERS set, skipping rust server builds...
)

endlocal
exit /b 0

rem ---------------------------------------------------------------------------
rem Build a Vite/npm frontend and wipe stale hashed outputs.
rem   %1 = project path relative to repo root (e.g. web\px_web_client)
rem   %2 = collect_dist folder name under build_official\dist (e.g. web_client)
rem ---------------------------------------------------------------------------
:build_npm_web
set "WEB_PROJ_DIR=%~dp0%~1"
set "WEB_DIST_NAME=%~2"
if not exist "%WEB_PROJ_DIR%\package.json" (
    echo ERROR: web project not found: %WEB_PROJ_DIR%
    exit /b 1
)

where npm >nul 2>&1
if errorlevel 1 (
    echo ERROR: npm not found in PATH. Install Node.js and retry.
    exit /b 1
)

echo ----------------------WEB FRONTEND-----------------------
echo Building: %WEB_PROJ_DIR%  -^>  build_official\dist\%WEB_DIST_NAME%

rem Wipe previous outputs so hashed vite assets cannot linger.
if exist "%WEB_PROJ_DIR%\dist" (
    echo Removing stale %WEB_PROJ_DIR%\dist ...
    rmdir /s /q "%WEB_PROJ_DIR%\dist"
)
if exist "%~dp0build_official\dist\%WEB_DIST_NAME%" (
    echo Removing stale %~dp0build_official\dist\%WEB_DIST_NAME% ...
    rmdir /s /q "%~dp0build_official\dist\%WEB_DIST_NAME%"
)

pushd "%WEB_PROJ_DIR%"
rem Always sync deps so package.json additions cannot leave a stale node_modules.
echo npm ci...
call npm ci
if errorlevel 1 (
    echo npm ci failed, falling back to npm install...
    call npm install
    if errorlevel 1 (
        popd
        echo ERROR: npm install failed in %~1
        exit /b 1
    )
)
echo npm run build...
call npm run build
if errorlevel 1 (
    popd
    echo ERROR: npm run build failed in %~1
    exit /b 1
)
popd

if not exist "%WEB_PROJ_DIR%\dist\index.html" (
    echo ERROR: %~1 build did not produce dist\index.html
    exit /b 1
)
echo Frontend build OK: %WEB_PROJ_DIR%\dist
echo ---------------------------------------------------------
exit /b 0
