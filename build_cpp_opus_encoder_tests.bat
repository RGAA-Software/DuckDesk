@echo off
setlocal

rem Focused built-in Opus encoder lifecycle tests. This does not build
rem Rust, web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_opus_encoder_runtime test_opus_encoder_processor px_render check_cpp_ownership
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build_official" -R "^(opus_encoder_runtime|opus_encoder_processor)$" --output-on-failure
exit /b %errorlevel%
