@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "WORKSPACE_DIR=%SCRIPT_DIR%.."
set "BIN_NAME=%~1"

if "%BIN_NAME%"=="" (
    set "BIN_NAME=GrSysMonitor"
)

echo [gr_sysinfo] Building debug binary: %BIN_NAME%
pushd "%WORKSPACE_DIR%"
if errorlevel 1 (
    echo [gr_sysinfo] Failed to enter workspace: %WORKSPACE_DIR%
    exit /b 1
)

cargo build -p gr_sysinfo --bin %BIN_NAME%
if errorlevel 1 (
    echo [gr_sysinfo] Build failed for %BIN_NAME%
    popd
    exit /b 1
)

echo [gr_sysinfo] Build finished:
echo %WORKSPACE_DIR%\target\debug\%BIN_NAME%.exe

popd
exit /b 0
