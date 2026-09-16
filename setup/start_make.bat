@echo off
if "%~1"=="" (
    echo Usage: %~nx0 ^<cloud_node^|client^|remote^> [dist-dir]
    exit /b 2
)

if "%~2"=="" (
    python make_setup.py --product "%~1"
) else (
    python make_setup.py --product "%~1" --dist-dir "%~2"
)
if errorlevel 1 exit /b %errorlevel%

exit /b 0
