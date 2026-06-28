@echo off
setlocal enabledelayedexpansion

echo [BAT] Detecting latest Visual Studio...
for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VS_PATH=%%i
if "%VS_PATH%"=="" (
    echo [BAT] ERROR: Could not find Visual Studio installation.
    exit /b 1
)
echo [BAT] Found VS at: %VS_PATH%

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64

cd /d "%~dp0..\build_official"
echo [BAT] Now in %CD%

ninja -j8 test_file test_file_util test_folder_util test_string_util test_auto_start test_win_helper test_dxgi_mon_detector test_network_adapter test_qr_generator test_process > build_tc_tests2.log 2>&1
set EXITCODE=%ERRORLEVEL%
echo BUILD_EXIT=%EXITCODE%
exit /b %EXITCODE%
