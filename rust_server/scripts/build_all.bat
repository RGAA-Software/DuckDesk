@echo off
setlocal

rem Build all programs in the rust_server workspace.
rem Run this script from anywhere; it will switch to the workspace root.

cd /d "%~dp0\.."

echo Building rust_server workspace ...
cargo build --workspace --release
if errorlevel 1 (
    echo Build failed.
    exit /b %errorlevel%
)

echo.
echo Build succeeded. Binaries are in target\release\

endlocal
