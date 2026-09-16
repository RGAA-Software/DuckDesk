@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0build_cpp_target.bat" test_string_util test_file test_file_util test_folder_util test_auto_start test_win_helper test_dxgi_mon_detector test_network_adapter test_qr_generator
exit /b %errorlevel%
