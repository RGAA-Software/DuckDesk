@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0..\scripts\build_cpp_target.bat" px_panel px_panel_product_tests px_ui_localization_tests
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\build_official\src\px_deps\px_panel_product_tests.exe" --gtest_color=no
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\build_official\src\px_ui\px_ui_localization_tests.exe" --gtest_color=no
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component panel
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\check_no_qt.ps1"
exit /b %errorlevel%
