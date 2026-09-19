@echo off
setlocal
cd /d "%~dp0.." || exit /b 1

if not "%~1"=="" (
    echo Usage: %~nx0
    exit /b 2
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0clean_product_outputs.ps1" -Product all
if errorlevel 1 exit /b %errorlevel%

call "%~dp0build_cloud_node.bat"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0build_client_product.bat"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0build_remote_product.bat"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0build_android_product.bat" official release
exit /b %errorlevel%
