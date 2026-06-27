@echo off
setlocal

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

for /f "delims=" %%a in ('dir /b /ad "%VS_INSTALL_DIR%\VC\Tools\MSVC" ^| "%SystemRoot%\System32\sort.exe" /r ^| findstr.exe /r "^[0-9]"') do (
    set "VC_TOOLS_DIR=%VS_INSTALL_DIR%\VC\Tools\MSVC\%%a\bin\Hostx64\x64"
    goto :found_vc
)
:found_vc
if not "%VC_TOOLS_DIR%"=="" (
    set "PATH=%VC_TOOLS_DIR%;%PATH%"
    echo Using VC tools: %VC_TOOLS_DIR%
)

if /I "%1"=="incremental" (
    echo Incremental build requested, skipping CMake configure...
    goto :do_build
)

cmake -S . -B build_official -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official -Wno-dev
if errorlevel 1 exit /b %errorlevel%

:do_build
echo ----------------------BUILD START------------------------
echo ---------------------------------------------------------
echo ---------------------------------------------------------
cmake --build build_official -j18
if errorlevel 1 exit /b %errorlevel%

endlocal
