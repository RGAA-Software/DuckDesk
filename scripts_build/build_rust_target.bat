@echo off
setlocal
rem Focused Rust build only: no version bump, web build, deployment or config replacement.
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote service^|user-proxy
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="client" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node, client, or remote.
    exit /b 2
)
set "RUST_PRODUCT=%~1"
if /i "%~2"=="service" (
    set "RUST_WORKSPACE=rust_client"
    set "RUST_PACKAGE=px_service"
) else if /i "%~2"=="user-proxy" (
    set "RUST_WORKSPACE=rust_client"
    set "RUST_PACKAGE=px_user_proxy"
) else (
    echo Usage: %~nx0 cloud_node^|client^|remote service^|user-proxy
    exit /b 2
)
set "RUST_REPO=%~dp0.."
set "CARGO_TARGET_DIR=%RUST_REPO%\build_official\%RUST_PRODUCT%\cargo\target"
set "RUST_VS="
for /f "usebackq delims=" %%V in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RUST_VS=%%V"
if not defined RUST_VS exit /b 2
call "%RUST_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=%RUST_REPO%\tools\nasm;%PATH%"
set "PROTOC=%RUST_REPO%\tools\protoc.exe"
set "CMAKE_GENERATOR=Ninja"
set "AWS_LC_SYS_C_STD=11"
set "GOOS=windows"
if exist "%ProgramFiles%\Git\usr\bin\perl.exe" set "PATH=%PATH%;%ProgramFiles%\Git\usr\bin"
cd /d "%RUST_REPO%\%RUST_WORKSPACE%" || exit /b 2
cargo build --release -p %RUST_PACKAGE%
exit /b %errorlevel%
