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

rem Focused built-in live-pusher tests. This does not build Rust, web assets,
rem bump release versions, or produce a live-pusher plug-in DLL.
call "%~dp0..\scripts\build_cpp_target.bat" test_live_pusher_sink test_live_pusher_ffmpeg
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -R "^(live_pusher_sink|live_pusher_ffmpeg)$" --output-on-failure
exit /b %errorlevel%
