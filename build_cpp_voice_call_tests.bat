@echo off
setlocal

rem Focused built-in voice-call runtime, service, and transport tests.
rem This does not build Rust, web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_voice_call test_voice_call_runtime test_voice_call_service px_render check_cpp_ownership
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build_official" -R "^(voice_call_core|render_voice_call_runtime|render_voice_call_service)$" --output-on-failure
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component render
exit /b %errorlevel%
