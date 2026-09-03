@echo off
setlocal enabledelayedexpansion

rem WAS audio is a built-in Render Source. Build its focused tests.
rem Same VS / MSVC environment discovery as build_official.bat.

cd /d "%~dp0..\.."

set "TARGET=test_was_audio_capture_source"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL_DIR="

if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Community"
if "%VS_INSTALL_DIR%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Professional"
if "%VS_INSTALL_DIR%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise"
if "%VS_INSTALL_DIR%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\18\BuildTools"
if "%VS_INSTALL_DIR%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if "%VS_INSTALL_DIR%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
if "%VS_INSTALL_DIR%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
if "%VS_INSTALL_DIR%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if "%VS_INSTALL_DIR%"=="" if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_DIR=%%i"
    )
)

if "%VS_INSTALL_DIR%"=="" (
    echo Failed to find Visual Studio 2022/2026 with MSVC x64 tools.
    exit /b 1
)

call "%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

set "VC_TOOLS_DIR="
pushd "%VS_INSTALL_DIR%\VC\Tools\MSVC" >nul 2>&1
if not errorlevel 1 (
    for /f "delims=" %%a in ('dir /b /ad ^| "%SystemRoot%\System32\sort.exe" /r') do (
        echo %%a | findstr /r "^[0-9]" >nul
        if not errorlevel 1 (
            set "VC_TOOLS_DIR=%VS_INSTALL_DIR%\VC\Tools\MSVC\%%a\bin\Hostx64\x64"
            goto :found_vc
        )
    )
)
:found_vc
popd >nul 2>&1
if not "%VC_TOOLS_DIR%"=="" (
    set "PATH=%VC_TOOLS_DIR%;%PATH%"
    for %%I in ("%VC_TOOLS_DIR%\..\..\..") do set "VC_MSVC_DIR=%%~fI"
    set "LIB=!VC_MSVC_DIR!\lib\x64;!VC_MSVC_DIR!\ATLMFC\lib\x64;%LIB%"
    set "INCLUDE=!VC_MSVC_DIR!\include;%INCLUDE%"
    echo Using VC tools: %VC_TOOLS_DIR%
)

if not exist "build_official\build.ninja" (
    echo build_official not found. Run build_official.bat first ^(or with full/reconfigure^).
    exit /b 1
)

echo Building target: %TARGET%
cmake --build build_official -j18 --target %TARGET%
if errorlevel 1 exit /b %errorlevel%

echo DONE: %TARGET%
endlocal
