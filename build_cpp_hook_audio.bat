@echo off
setlocal
if not "%~1"=="" set "CPP_BUILD_JOBS=%~1"
call "%~dp0scripts\build_cpp_target.bat" test_hook_audio_worker_lifecycle px_gh
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish_cpp_artifacts.ps1" -Component hook_audio
exit /b %errorlevel%
