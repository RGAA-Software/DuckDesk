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
call "%~dp0..\scripts\build_cpp_target.bat" test_sdk_hello_lifecycle test_client_imgui_launch_config
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -R "^(sdk_hello_lifecycle|client_imgui_launch_config)$" --output-on-failure
exit /b %errorlevel%
