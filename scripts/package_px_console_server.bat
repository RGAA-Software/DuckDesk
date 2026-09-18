@echo off
pwsh.exe -NoProfile -File "%~dp0package_px_console_server.ps1"
exit /b %errorlevel%
