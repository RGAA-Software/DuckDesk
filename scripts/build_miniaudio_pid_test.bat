@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL_DIR="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL_DIR=%%i"
)
if "%VS_INSTALL_DIR%"=="" (
    echo Failed to find Visual Studio with MSVC.
    exit /b 1
)

call "%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

for /f "delims=" %%a in ('dir /b /ad "%VS_INSTALL_DIR%\VC\Tools\MSVC" ^| "%SystemRoot%\System32\sort.exe" /r ^| findstr.exe /r "^[0-9]"') do (
    set "VC_TOOLS_DIR=%VS_INSTALL_DIR%\VC\Tools\MSVC\%%a\bin\Hostx64\x64"
    goto :found_vc
)
:found_vc
if not "%VC_TOOLS_DIR%"=="" (
    set "PATH=%VC_TOOLS_DIR%;%PATH%"
    for %%I in ("%VC_TOOLS_DIR%\..\..\..") do set "VC_MSVC_DIR=%%~fI"
    set "LIB=!VC_MSVC_DIR!\lib\x64;!VC_MSVC_DIR!\ATLMFC\lib\x64;%LIB%"
    set "INCLUDE=!VC_MSVC_DIR%\include;%INCLUDE%"
)

cmake -S . -B build_official -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official -Wno-dev
if errorlevel 1 exit /b %errorlevel%

cmake --build build_official --target test_miniaudio_pid_loopback plugin_was_audio_capture -j18
exit /b %errorlevel%
