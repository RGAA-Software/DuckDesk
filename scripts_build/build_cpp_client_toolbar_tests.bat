@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" test_client_controller_position test_client_imgui_launch_config test_client_statistics test_client_screenshot test_client_recording_feedback test_sdk_recording_session px_ui_component_tests px_ui_localization_tests
if errorlevel 1 exit /b %errorlevel%
ctest.exe --test-dir "%~dp0..\%CPP_BUILD_DIR%" --output-on-failure -R "^(client_(controller_position|imgui_launch_config|statistics|screenshot|recording_feedback)|sdk_recording_session|px_ui_(component|localization)_tests)$"
exit /b %errorlevel%
