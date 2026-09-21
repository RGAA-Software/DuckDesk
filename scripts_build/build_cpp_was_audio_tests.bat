@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|remote [jobs]
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node or remote.
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"

rem Focused WAS audio lifecycle and capture tests. This does not build Rust,
rem web assets, or bump release versions.
call "%~dp0..\scripts\build_cpp_target.bat" test_was_audio_capture_source test_was_audio_capture_runtime test_process_loopback_lifecycle test_miniaudio_reinit_cancel test_was_audio_capture_hardware test_wasapi_tone
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -R "^(was_audio_capture_source|was_audio_capture_runtime|process_loopback_lifecycle|miniaudio_reinit_cancel|was_audio_capture_hardware)$" --output-on-failure
exit /b %errorlevel%
