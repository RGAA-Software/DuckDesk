@echo off
setlocal
if "%~1"=="" (
    echo Usage: build_cpp_render_network_library.bat target [parallelism]
    exit /b 2
)
set "LIBRARY_TARGET=%~1"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0scripts\build_cpp_target.bat" %LIBRARY_TARGET%
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component render_network_library -LibraryTarget "%LIBRARY_TARGET%"
exit /b %errorlevel%
