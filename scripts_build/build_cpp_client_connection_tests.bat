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
call "%~dp0..\scripts\build_cpp_target.bat" test_client_controller_position test_client_statistics test_client_imgui_launch_config test_iroh_sdk_connection test_client_rdp_lifecycle test_client_audio_output
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" --output-on-failure -R "^(client_(controller_position|statistics|imgui_launch_config|rdp_lifecycle|audio_output)|iroh_sdk_connection)$"
exit /b %errorlevel%
