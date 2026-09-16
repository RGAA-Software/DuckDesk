@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" px_client px_rtc_client test_client_imgui_launch_config test_client_input_mapper test_client_instance_guard test_client_local_file_system test_client_file_browser_model test_client_file_transfer_format
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\%CPP_BUILD_DIR%\src\px_client\test_client_imgui_launch_config.exe"
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\%CPP_BUILD_DIR%\src\px_client\test_client_input_mapper.exe"
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\%CPP_BUILD_DIR%\src\px_client\test_client_instance_guard.exe"
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\%CPP_BUILD_DIR%\src\px_client\test_client_local_file_system.exe"
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\%CPP_BUILD_DIR%\src\px_client\test_client_file_browser_model.exe"
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\%CPP_BUILD_DIR%\src\px_client\test_client_file_transfer_format.exe"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component client -BuildDir "%CPP_BUILD_DIR%" -DistDir "build_official\%CPP_PRODUCT%\dist"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\check_no_qt.ps1" -Product "%CPP_PRODUCT%"
exit /b %errorlevel%
