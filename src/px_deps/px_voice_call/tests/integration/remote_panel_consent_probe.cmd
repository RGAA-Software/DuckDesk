@echo off
setlocal
set "PROBE_ACTION=%~1"
if "%PROBE_ACTION%"=="" set "PROBE_ACTION=Probe"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\px_stage\remote_panel_consent_probe.ps1" -Action "%PROBE_ACTION%" > "C:\px_stage\voice_probe_task.log" 2>&1
exit /b %errorlevel%
