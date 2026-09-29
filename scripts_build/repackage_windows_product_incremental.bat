@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="client" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node, client, or remote.
    exit /b 2
)
if not "%~2"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote
    exit /b 2
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows_product_matrix.ps1" -Product "%~1" -Incremental
exit /b %errorlevel%
