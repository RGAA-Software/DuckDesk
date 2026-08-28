@echo off
setlocal
if "%~1"=="" (
    echo Usage: build_cpp_render_plugin.bat target [parallelism]
    exit /b 2
)
set "PLUGIN_TARGET=%~1"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0scripts\build_cpp_target.bat" %PLUGIN_TARGET%
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component render_plugin -PluginTarget "%PLUGIN_TARGET%"
exit /b %errorlevel%
