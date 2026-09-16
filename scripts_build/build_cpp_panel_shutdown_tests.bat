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
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"

rem Focused Panel shutdown orchestration test and Panel build. This does not
rem build Rust/web workspaces or bump release versions.
call "%~dp0..\scripts\build_cpp_target.bat" test_weak_callback test_panel_shutdown_sequence test_panel_running_pipe_lifecycle test_panel_auth_manager_lifecycle test_panel_win_message_window_lifecycle test_panel_qt_lifetime_guard px_panel
exit /b %errorlevel%
