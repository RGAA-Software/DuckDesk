@echo off
pwsh.exe -NoProfile -File "%~dp0package_px_auth_server.ps1"
exit /b %errorlevel%
