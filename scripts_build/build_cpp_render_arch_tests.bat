@echo off
setlocal

rem Focused Render architecture runner. This never invokes build_official.bat.
rem Usage: build_cpp_render_arch_tests.bat cloud_node^|remote [quick^|lifecycle^|integration^|hardware^|all^|performance] [jobs]

if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|remote [quick^|lifecycle^|integration^|hardware^|all^|performance] [jobs]
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node or remote.
    exit /b 2
)
set "PRODUCT=%~1"
set "MODE=all"
set "JOBS=8"

if not "%~2"=="" set "MODE=%~2"
if not "%~3"=="" set "JOBS=%~3"

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0..\scripts\run_render_arch_tests.ps1" ^
    -Product "%PRODUCT%" ^
    -Mode "%MODE%" ^
    -Jobs "%JOBS%"
exit /b %errorlevel%
