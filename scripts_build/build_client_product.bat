@echo off
setlocal
cd /d "%~dp0.." || exit /b 1

if not "%~1"=="" (
    echo Usage: %~nx0
    exit /b 2
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows_product_matrix.ps1" -Product client
exit /b %errorlevel%
