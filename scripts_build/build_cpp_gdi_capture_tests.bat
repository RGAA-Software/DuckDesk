@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|remote [jobs]
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node or remote.
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"

rem Focused built-in GDI capture and Render linkage tests. This does not build
rem Rust, web assets, bump release versions, or produce a capture plug-in DLL.
call "%~dp0..\scripts\build_cpp_target.bat" cap_gdi test_render_builtin_linkage check_cpp_ownership
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -R "^render_builtin_linkage$" --output-on-failure
exit /b %errorlevel%
