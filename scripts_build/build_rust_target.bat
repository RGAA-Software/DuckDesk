@echo off
setlocal
rem Focused Rust build only: no version bump, web build, deployment or config replacement.
if /i "%~1"=="console" (
    set "RUST_WORKSPACE=rust_server"
    set "RUST_PACKAGE=px_console_server"
) else if /i "%~1"=="service" (
    set "RUST_WORKSPACE=rust_client"
    set "RUST_PACKAGE=px_service"
) else (
    echo Usage: build_rust_target.bat console^|service
    exit /b 2
)
set "RUST_REPO=%~dp0.."
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
