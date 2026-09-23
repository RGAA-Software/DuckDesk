@echo off
setlocal

rem Focused development build. It does not bump a version or create a release package.
cd /d "%~dp0.." || exit /b 1
set "REPO_ROOT=%cd%"
set "TARGET_ROOT=%REPO_ROOT%\.cache\relay-dev"
set "OUTPUT_ROOT=%REPO_ROOT%\output\px_relay\dev"

where cargo.exe >nul 2>nul || (echo ERROR: cargo.exe is required.& exit /b 1)

set "SQLX_OFFLINE=true"
cargo.exe build --locked --release --manifest-path "%REPO_ROOT%\rust_server\Cargo.toml" -p px_relay_server --bin px_relay --target-dir "%TARGET_ROOT%"
if errorlevel 1 exit /b 1

if not exist "%OUTPUT_ROOT%" mkdir "%OUTPUT_ROOT%"
copy /Y "%TARGET_ROOT%\release\px_relay.exe" "%OUTPUT_ROOT%\px_relay.exe" >nul
if errorlevel 1 exit /b 1

pwsh.exe -NoProfile -Command "$source=(Get-FileHash -LiteralPath '%TARGET_ROOT%\release\px_relay.exe').Hash; $copy=(Get-FileHash -LiteralPath '%OUTPUT_ROOT%\px_relay.exe').Hash; if($source -ne $copy){throw 'px_relay.exe hash mismatch'}; Write-Host ('HASH OK px_relay.exe ' + $copy)"
if errorlevel 1 exit /b 1

echo Relay development build: %OUTPUT_ROOT%
exit /b 0
