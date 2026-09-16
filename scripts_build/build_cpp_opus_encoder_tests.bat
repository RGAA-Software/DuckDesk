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

rem Focused built-in Opus encoder lifecycle tests. This does not build
rem Rust, web assets, or bump release versions.
call "%~dp0..\scripts\build_cpp_target.bat" test_opus_encoder_runtime test_opus_encoder_processor px_render check_cpp_ownership
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -R "^(opus_encoder_runtime|opus_encoder_processor)$" --output-on-failure
exit /b %errorlevel%
