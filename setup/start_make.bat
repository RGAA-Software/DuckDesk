@echo off
if "%~1"=="" (
    echo Usage: %~nx0 ^<cloud_node^|client^|remote^> official [dist-dir]
    exit /b 2
)
if /I not "%~2"=="official" (
    echo ERROR: Pixels installers use the single official publisher identity.
    exit /b 2
)

if "%~3"=="" (
    python make_setup.py --product "%~1" --distribution "%~2"
) else (
    python make_setup.py --product "%~1" --distribution "%~2" --dist-dir "%~3"
)
if errorlevel 1 exit /b %errorlevel%

exit /b 0
