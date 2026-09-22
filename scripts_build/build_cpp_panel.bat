@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" px_panel px_panel_product_tests px_ui_localization_tests
if errorlevel 1 exit /b %errorlevel%
ctest.exe --test-dir "%~dp0..\%CPP_BUILD_DIR%" --output-on-failure -R "^px_panel_product_tests$"
if errorlevel 1 exit /b %errorlevel%
ctest.exe --test-dir "%~dp0..\%CPP_BUILD_DIR%" --output-on-failure -R "^px_ui_localization_tests$"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component panel -BuildDir "%CPP_BUILD_DIR%" -DistDir "build_official\%CPP_PRODUCT%\dist"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\check_no_qt.ps1" -Product "%CPP_PRODUCT%"
exit /b %errorlevel%
