@echo off
setlocal
cd /d "%~dp0.." || exit /b 1

if /I "%~1"=="debug" goto :run
if /I "%~1"=="release" goto :run

echo Usage: %~nx0 debug [install] ^| release
exit /b 2

:run
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_android_product.ps1" -Configuration "%~1" -Action "%~2"
exit /b %errorlevel%
