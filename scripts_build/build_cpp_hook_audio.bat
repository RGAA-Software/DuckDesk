@echo off
setlocal
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote [jobs]
    exit /b 2
)
set "CPP_PRODUCT=%~1"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
if not "%~2"=="" set "CPP_BUILD_JOBS=%~2"
call "%~dp0..\scripts\build_cpp_target.bat" test_hook_audio_worker_lifecycle test_ws_ipc_client_lifecycle px_gh
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\publish_cpp_artifacts.ps1" -Component hook_audio -BuildDir "%CPP_BUILD_DIR%" -DistDir "build_official\%CPP_PRODUCT%\dist"
exit /b %errorlevel%
