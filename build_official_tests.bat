@echo off
setlocal enabledelayedexpansion

rem Keep MSVC /showIncludes output compatible with Ninja dependency parsing.
set "VSLANG=1033"

cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL_DIR="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version "[18.0,19.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_DIR=%%i"
    )
)

if "%VS_INSTALL_DIR%"=="" (
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Community"
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Professional"
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise"
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\BuildTools"
)

if "%VS_INSTALL_DIR%"=="" (
    if exist "%VSWHERE%" (
        for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VS_INSTALL_DIR=%%i"
        )
    )
)

if "%VS_INSTALL_DIR%"=="" (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
)

if "%VS_INSTALL_DIR%"=="" (
    echo Failed to find Visual Studio 2022/2026 with MSVC x64 tools.
    exit /b 1
)

call "%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
rem VsDevCmd may overwrite VSLANG, so enforce it after environment setup too.
set "VSLANG=1033"

for /f "delims=" %%a in ('dir /b /ad "%VS_INSTALL_DIR%\VC\Tools\MSVC" ^| "%SystemRoot%\System32\sort.exe" /r ^| findstr.exe /r "^[0-9]"') do (
    set "VC_TOOLS_DIR=%VS_INSTALL_DIR%\VC\Tools\MSVC\%%a\bin\Hostx64\x64"
    goto :found_vc
)
:found_vc
if not "%VC_TOOLS_DIR%"=="" (
    set "PATH=%VC_TOOLS_DIR%;%PATH%"
    for %%I in ("%VC_TOOLS_DIR%\..\..\..") do set "VC_MSVC_DIR=%%~fI"
    set "LIB=!VC_MSVC_DIR!\lib\x64;!VC_MSVC_DIR!\ATLMFC\lib\x64;%LIB%"
    set "INCLUDE=!VC_MSVC_DIR!\include;%INCLUDE%"
    echo Using VC tools: %VC_TOOLS_DIR%
    echo Using VC libs: !VC_MSVC_DIR!\lib\x64
)

set "SKIP_CONFIGURE=0"
set "RUN_TESTS=0"
for %%a in (%*) do (
    if /I "%%a"=="incremental" set "SKIP_CONFIGURE=1"
    if /I "%%a"=="run" set "RUN_TESTS=1"
)

if "%SKIP_CONFIGURE%"=="0" (
    cmake -S . -B build_official -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official -Wno-dev
    if errorlevel 1 exit /b %errorlevel%
)

echo ----------------------BUILD TESTS START------------------------
echo ---------------------------------------------------------
cmake --build build_official -j18 --target ^
    px_common_new ^
    test_string_util test_file_util test_folder_util test_file ^
    test_auto_start test_win_helper test_dxgi_mon_detector test_network_adapter ^
    test_qr_generator test_process test_process_helper test_uncovered test_snowflake_id ^
    test_clipboard_echo test_clipboard_file_builder test_clipboard_platform ^
    test_px_udp_protocol ^
    test_ft_path_security test_ft_compress test_ft_transfer_job test_ft_engine ^
    px_media_record_new test_record_writer ^
    test_records_catalog test_records_ticket test_record_transfer ^
    test_common test_http test_cpu
if errorlevel 1 exit /b %errorlevel%

echo ----------------------BUILD TESTS DONE-------------------------

if "%RUN_TESTS%"=="1" (
    call "%~dp0scripts\run_tc_tests.bat"
    if errorlevel 1 exit /b %errorlevel%
)

endlocal
