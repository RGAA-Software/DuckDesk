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

rem Focused built-in media recorder lifecycle/remux tests. This does not build
rem Rust, web assets, bump release versions, or produce a recorder plug-in DLL.
call "%~dp0..\scripts\build_cpp_target.bat" test_record_writer test_media_recorder_sink
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%CPP_BUILD_DIR%" -R "^(render_record_writer|media_recorder_sink)$" --output-on-failure
exit /b %errorlevel%
