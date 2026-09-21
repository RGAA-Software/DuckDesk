@echo off
setlocal
cd /d "%~dp0.." || exit /b 1

if "%~1"=="" goto :usage
if /I not "%~1"=="cloud_node" if /I not "%~1"=="client" if /I not "%~1"=="remote" goto :usage
if "%~2"=="" goto :build
if /I "%~2"=="preflight" goto :preflight
goto :usage

:build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows_oem_product.ps1" -Product "%~1"
exit /b %errorlevel%

:preflight
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows_oem_product.ps1" -Product "%~1" -PreflightOnly
exit /b %errorlevel%

:usage
echo Usage: %~nx0 cloud_node^|client^|remote [preflight]
exit /b 2
