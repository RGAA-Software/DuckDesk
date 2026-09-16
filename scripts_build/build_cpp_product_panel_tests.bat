@echo off
setlocal

if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote [jobs]
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="client" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node, client, or remote.
    exit /b 2
)

set "CPP_PRODUCT=%~1"
if not defined CPP_BUILD_DIR set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"

call "%~dp0..\scripts\build_cpp_target.bat" px_panel_product_tests
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" --output-on-failure -R "^px_panel_product_tests$"
exit /b %errorlevel%
