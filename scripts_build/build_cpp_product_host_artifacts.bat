@echo off
setlocal

if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote [jobs]
    exit /b 2
)

set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"

call "%~dp0..\scripts\build_cpp_target.bat" px_display_build px_gh px_gh_injector px_gh_address
exit /b %errorlevel%
