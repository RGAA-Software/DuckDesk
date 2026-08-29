@echo off
setlocal

rem Focused WAS audio lifecycle and capture tests. This does not build Rust,
rem web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_was_audio_capture_runtime test_was_audio_plugin_dll_lifecycle test_miniaudio_reinit_cancel test_plugin_was_audio_capture cap_was_audio
exit /b %errorlevel%
