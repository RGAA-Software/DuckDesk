@echo off
setlocal

rem Focused voice-call runtime, transport, and plug-in lifecycle tests.
rem This does not build Rust, web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_voice_call test_voice_call_runtime test_voice_call_plugin_dll_lifecycle test_client_voice_call_protocol voice_call px_render px_client
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component render
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component render_plugin -PluginTarget voice_call
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component client
exit /b %errorlevel%
