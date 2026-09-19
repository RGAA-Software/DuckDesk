@echo off
if "%~1"=="" (
    echo Usage: %~nx0 ^<cloud_node^|client^|remote^> ^<official^|customer^> [dist-dir]
    exit /b 2
)
if /I not "%~2"=="official" if /I not "%~2"=="customer" (
    echo ERROR: distribution must be official or customer.
    exit /b 2
)

if "%~3"=="" (
    python make_setup.py --product "%~1" --distribution "%~2"
) else (
    python make_setup.py --product "%~1" --distribution "%~2" --dist-dir "%~3"
)
if errorlevel 1 exit /b %errorlevel%

exit /b 0
