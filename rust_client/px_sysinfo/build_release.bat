@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "WORKSPACE_DIR=%SCRIPT_DIR%.."
set "BIN_NAME=%~1"

if "%BIN_NAME%"=="" (
    set "BIN_NAME=px_sys_monitor"
)

echo [px_sysinfo] Building release binary: %BIN_NAME%
pushd "%WORKSPACE_DIR%"
if errorlevel 1 (
    echo [px_sysinfo] Failed to enter workspace: %WORKSPACE_DIR%
    exit /b 1
)

cargo build -p px_sysinfo --release --bin %BIN_NAME%
if errorlevel 1 (
    echo [px_sysinfo] Build failed for %BIN_NAME%
    popd
    exit /b 1
)

echo [px_sysinfo] Build finished:
echo %WORKSPACE_DIR%\target\release\%BIN_NAME%.exe

popd
exit /b 0
