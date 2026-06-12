@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo Building rust_server workspace (all targets, release mode)
echo ============================================================
echo.

rem Ensure we run from the repository root
cd /d "%~dp0"

rem Verify cargo is available
cargo --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: cargo is not found in PATH.
    echo Please install Rust or run this script from a developer environment.
    exit /b 1
)

echo cargo version:
cargo --version
echo rustc version:
rustc --version
echo.

set "WORKSPACE_DIR=rust_server"
if not exist "%WORKSPACE_DIR%\Cargo.toml" (
    echo ERROR: %WORKSPACE_DIR%\Cargo.toml not found.
    echo Please run this script from the repository root.
    exit /b 1
)

cd /d "%WORKSPACE_DIR%"

echo Building all workspace members in release mode...
echo.

cargo build --workspace --release
if errorlevel 1 (
    echo.
    echo ERROR: rust_server release build failed.
    exit /b %errorlevel%
)

echo.
echo ============================================================
echo rust_server release build succeeded.
echo ============================================================
echo.
echo Generated binaries in %WORKSPACE_DIR%\target\release\:
echo ------------------------------------------------------------

set "BIN_COUNT=0"
for %%f in (target\release\*.exe) do (
    echo   %%~nxf
    set /a BIN_COUNT+=1
)

echo ------------------------------------------------------------
echo Total executables: %BIN_COUNT%
echo.

endlocal
