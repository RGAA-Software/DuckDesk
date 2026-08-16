@echo off
chcp 65001 >nul
echo.
echo Searching for px_render.exe processes ...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0list_gammaray_render.ps1" -Name "px_render.exe"

if %ERRORLEVEL% neq 0 (
    echo Failed to run PowerShell script.
)

pause
