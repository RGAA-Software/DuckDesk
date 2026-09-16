@echo off
setlocal
call "%~dp0build_rust_target.bat" service
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_rust_artifacts.ps1" -Component service
exit /b %errorlevel%
