@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0..\scripts\build_cpp_target.bat" px_panel_imgui px_ui_localization_tests skin_interface skin_loader skin_official skin_opensource
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_panel_imgui.ps1"
exit /b %errorlevel%
