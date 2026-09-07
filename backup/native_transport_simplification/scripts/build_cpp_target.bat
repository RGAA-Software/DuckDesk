@echo off
setlocal enabledelayedexpansion

rem Incremental C++ target builder. This script never bumps the product version,
rem runs npm, invokes Cargo, collects the complete dist tree, or builds servers.
rem Usage: scripts\build_cpp_target.bat target [target ...]
rem Optional environment: CPP_BUILD_DIR (default build_official), CPP_BUILD_JOBS (default 8)

cd /d "%~dp0\.."
if "%~1"=="" (
    echo ERROR: at least one CMake target is required.
    echo Usage: scripts\build_cpp_target.bat target [target ...]
    exit /b 2
)

set "BUILD_DIR=%CPP_BUILD_DIR%"
if not defined BUILD_DIR set "BUILD_DIR=build_official"
set "BUILD_JOBS=%CPP_BUILD_JOBS%"
if not defined BUILD_JOBS set "BUILD_JOBS=8"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL_DIR="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL_DIR=%%I"
)
if not defined VS_INSTALL_DIR (
    for %%V in (
        "%ProgramFiles%\Microsoft Visual Studio\18\Community"
        "%ProgramFiles%\Microsoft Visual Studio\18\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
    ) do if not defined VS_INSTALL_DIR if exist "%%~V\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%%~V"
)
if not defined VS_INSTALL_DIR (
    echo ERROR: Visual Studio with MSVC x64 tools was not found.
    exit /b 1
)

call "%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "VSLANG=1033"

if not exist "%BUILD_DIR%\build.ninja" (
    echo C++ build tree does not exist; configuring CMake only: %CD%\%BUILD_DIR%
    cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official -Wno-dev
    if errorlevel 1 exit /b %errorlevel%
)

echo Incremental C++ build only. Build dir: %BUILD_DIR%, jobs: %BUILD_JOBS%
echo Targets: %*
cmake --build "%BUILD_DIR%" --parallel %BUILD_JOBS% --target %*
if errorlevel 1 exit /b %errorlevel%

echo DONE: %*
endlocal
