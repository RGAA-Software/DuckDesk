@echo off
setlocal

rem Focused WAS audio lifecycle and capture tests. This does not build Rust,
rem web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_was_audio_capture_source test_was_audio_capture_runtime test_process_loopback_lifecycle test_miniaudio_reinit_cancel test_was_audio_capture_hardware
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build_official" -R "^(was_audio_capture_source|was_audio_capture_runtime|process_loopback_lifecycle|miniaudio_reinit_cancel|was_audio_capture_hardware)$" --output-on-failure
exit /b %errorlevel%
