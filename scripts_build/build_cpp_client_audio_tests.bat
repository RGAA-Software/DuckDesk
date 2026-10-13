@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
set "CPP_BUILD_JOBS=18"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" test_client_audio_output
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\%CPP_BUILD_DIR%\src\px_client\test_client_audio_output.exe"
exit /b %errorlevel%
