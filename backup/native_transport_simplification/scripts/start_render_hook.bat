@echo off
rem Thin wrapper. Edit launch parameters in start_render_hook.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_render_hook.ps1" %*
exit /b %ERRORLEVEL%
