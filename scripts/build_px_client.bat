@echo off
setlocal

rem Build px_client, its built-in feature modules, and linked runtime libraries
rem with the project's existing build_official CMake/Ninja tree.
rem Usage: scripts\build_px_client.bat [build_dir] [parallelism]
rem Example: scripts\build_px_client.bat build_official 8

cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build_official"
set "PARALLEL=%~2"
if "%PARALLEL%"=="" set "PARALLEL=8"

if not exist "%BUILD_DIR%\build.ninja" (
    echo ERROR: CMake/Ninja build directory was not found: %REPO_ROOT%\%BUILD_DIR%
    echo Run the project's configure/build_official script first.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_ROOT="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_ROOT=%%I"
)

if not defined VS_ROOT (
    echo ERROR: No Visual Studio installation with MSVC x64 tools was found.
    exit /b 1
)

call "%VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

echo Building px_client and RTC transport in %BUILD_DIR% with %PARALLEL% jobs...
cmake --build "%BUILD_DIR%" --config RelWithDebInfo --parallel %PARALLEL% --target px_client px_rtc_client
if errorlevel 1 exit /b %errorlevel%

set "CLIENT_OUT=%BUILD_DIR%\src\px_client\px_client.exe"
set "RTC_OUT=%BUILD_DIR%\src\px_deps\px_webrtc_client\px_rtc_client.dll"
set "DIST_DIR=%BUILD_DIR%\dist"
set "FT_DIST_DIR=%DIST_DIR%\deps\ct_plugins"
set "LANG_SRC=%REPO_ROOT%\src\px_panel\resources\language"
set "LANG_DIST=%DIST_DIR%\resources\language"
if not exist "%CLIENT_OUT%" (
    echo ERROR: px_client build completed but output was not found: %CLIENT_OUT%
    exit /b 1
)
if not exist "%RTC_OUT%" (
    echo ERROR: RTC client build completed but output was not found: %RTC_OUT%
    exit /b 1
)
copy /Y "%CLIENT_OUT%" "%DIST_DIR%\px_client.exe" >nul || exit /b 1
copy /Y "%RTC_OUT%" "%DIST_DIR%\px_client_rtc.dll" >nul || exit /b 1
del /Q "%DIST_DIR%\px_client_recording_core.dll" 2>nul
del /Q "%FT_DIST_DIR%\clipboard.dll" 2>nul
del /Q "%FT_DIST_DIR%\ft.dll" 2>nul
del /Q "%FT_DIST_DIR%\record.dll" 2>nul
del /Q "%FT_DIST_DIR%\client_clipboard.dll" 2>nul
del /Q "%FT_DIST_DIR%\ft_client.dll" 2>nul
del /Q "%FT_DIST_DIR%\media_record_client.dll" 2>nul
cmake -E copy_directory "%LANG_SRC%" "%LANG_DIST%" || exit /b 1

echo DONE: published px_client.exe, RTC DLL and language files to %DIST_DIR%
endlocal
