@echo off
setlocal
pwsh -NoProfile -File "%~dp0package_px_desk_server.ps1"
exit /b %errorlevel%
