@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" test_desktop_shell_fullscreen test_title_bar_maximize
if errorlevel 1 exit /b %errorlevel%
ctest.exe --test-dir "%~dp0..\%CPP_BUILD_DIR%" --output-on-failure -R "^(desktop_shell_fullscreen|title_bar_maximize)$"
exit /b %errorlevel%
