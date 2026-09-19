@echo off
setlocal
cd /d "%~dp0.." || exit /b 1

if /I "%~1"=="official" goto :configuration
if /I "%~1"=="customer" goto :configuration

echo Usage: %~nx0 official^|customer debug [install] ^| official^|customer release
exit /b 2

:configuration
if /I "%~2"=="debug" goto :run
if /I "%~2"=="release" goto :run
echo Usage: %~nx0 official^|customer debug [install] ^| official^|customer release
exit /b 2

:run
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_android_product.ps1" -Distribution "%~1" -Configuration "%~2" -Action "%~3"
exit /b %errorlevel%
