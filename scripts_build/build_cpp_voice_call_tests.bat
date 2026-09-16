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

rem Focused built-in voice-call runtime, service, and transport tests.
rem This does not build Rust, web assets, or bump release versions.
call "%~dp0..\scripts\build_cpp_target.bat" test_voice_call test_voice_call_runtime test_voice_call_service px_render check_cpp_ownership
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -R "^(voice_call_core|render_voice_call_runtime|render_voice_call_service)$" --output-on-failure
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component render -BuildDir "%CPP_BUILD_DIR%" -DistDir "build_official\%CPP_PRODUCT%\dist"
exit /b %errorlevel%
