@echo off
setlocal

rem Focused Render architecture runner. This never invokes build_official.bat.
rem Usage: build_cpp_render_arch_tests.bat [quick^|lifecycle^|integration^|hardware^|all^|performance] [jobs]
rem Backward compatibility: a numeric first argument means "all" with that job count.

set "MODE=all"
set "JOBS=8"

if not "%~1"=="" (
    echo %~1| findstr /r "^[0-9][0-9]*$" >nul
    if not errorlevel 1 (
        set "JOBS=%~1"
    ) else (
        set "MODE=%~1"
        if not "%~2"=="" set "JOBS=%~2"
    )
)

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0scripts\run_render_arch_tests.ps1" ^
    -Mode "%MODE%" ^
    -Jobs "%JOBS%"
exit /b %errorlevel%
