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
if not defined CPP_BUILD_DIR set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"

call "%~dp0..\scripts\build_cpp_target.bat" px_render
exit /b %errorlevel%
