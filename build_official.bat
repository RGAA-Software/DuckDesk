@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL_DIR="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_DIR=%%i"
    )
)

if "%VS_INSTALL_DIR%"=="" (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
)

if "%VS_INSTALL_DIR%"=="" (
    echo Failed to find Visual Studio 2026 with MSVC x64 tools.
    exit /b 1
)

call "%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

cmake -S . -B build_official -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official
if errorlevel 1 exit /b %errorlevel%

echo ----------------------BUILD START------------------------
echo ---------------------------------------------------------
echo ---------------------------------------------------------
cmake --build build_official -j18
if errorlevel 1 exit /b %errorlevel%

endlocal
