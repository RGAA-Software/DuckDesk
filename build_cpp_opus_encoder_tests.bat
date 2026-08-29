@echo off
setlocal

rem Focused Opus encoder runtime/plugin lifecycle tests. This does not build
rem Rust, web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_opus_encoder_runtime test_opus_encoder_plugin_dll_lifecycle enc_opus
exit /b %errorlevel%
