@echo off
setlocal

rem Focused development build. It does not bump a version or create a release.
cd /d "%~dp0.." || exit /b 1
set "REPO_ROOT=%cd%"
set "WEB_ROOT=%REPO_ROOT%\web\px_console"
set "TARGET_ROOT=%REPO_ROOT%\.cache\console-dev"
set "OUTPUT_ROOT=%REPO_ROOT%\output\px_console\dev"

where npm.cmd >nul 2>nul || (echo ERROR: npm.cmd is required.& exit /b 1)
where cargo.exe >nul 2>nul || (echo ERROR: cargo.exe is required.& exit /b 1)

if not exist "%WEB_ROOT%\node_modules" (
    call npm.cmd --prefix "%WEB_ROOT%" ci --no-audit --no-fund
    if errorlevel 1 exit /b 1
)
call npm.cmd --prefix "%WEB_ROOT%" run build
if errorlevel 1 exit /b 1

set "SQLX_OFFLINE=true"
cargo.exe build --locked --release --manifest-path "%REPO_ROOT%\rust_server\Cargo.toml" -p px_console_runtime --bin px_console --target-dir "%TARGET_ROOT%"
if errorlevel 1 exit /b 1

if not exist "%OUTPUT_ROOT%" mkdir "%OUTPUT_ROOT%"
copy /Y "%TARGET_ROOT%\release\px_console.exe" "%OUTPUT_ROOT%\px_console.exe" >nul
if errorlevel 1 exit /b 1
if exist "%OUTPUT_ROOT%\static" rmdir /S /Q "%OUTPUT_ROOT%\static"
mkdir "%OUTPUT_ROOT%\static"
xcopy /E /I /Y "%WEB_ROOT%\dist\*" "%OUTPUT_ROOT%\static\" >nul
if errorlevel 1 exit /b 1

pwsh.exe -NoProfile -Command "$source=(Get-FileHash -LiteralPath '%TARGET_ROOT%\release\px_console.exe').Hash; $copy=(Get-FileHash -LiteralPath '%OUTPUT_ROOT%\px_console.exe').Hash; if($source -ne $copy){throw 'px_console.exe hash mismatch'}; Write-Host ('HASH OK px_console.exe ' + $copy)"
if errorlevel 1 exit /b 1

echo Console development build: %OUTPUT_ROOT%
echo Runtime configuration: docs\px_console_server_runtime_config.md
exit /b 0
