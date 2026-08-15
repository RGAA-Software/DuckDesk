@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%setup"
if errorlevel 1 (
    echo [px_sysinfo] failed to enter setup directory
    exit /b 1
)

call start_make.bat %*
set "EXIT_CODE=%ERRORLEVEL%"

popd
exit /b %EXIT_CODE%
