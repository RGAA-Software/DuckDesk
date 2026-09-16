@echo off
setlocal
if "%~1"=="" (
    echo Usage: %~nx0 cloud_node^|client^|remote
    exit /b 2
)
if /I not "%~1"=="cloud_node" if /I not "%~1"=="client" if /I not "%~1"=="remote" (
    echo ERROR: product must be cloud_node, client, or remote.
    exit /b 2
)
set "DEMO_PRODUCT=%~1"
set "DEMO_BUILD_DIR=build_official\%DEMO_PRODUCT%\tools\workspace_demo"
cd /d "%~dp0\.."
set "DEMO_VCPKG=%VCPKG_ROOT%"
if not defined DEMO_VCPKG set "DEMO_VCPKG=C:\source\vcpkg"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DEMO_VS=%%I"
if not defined DEMO_VS exit /b 1
call "%DEMO_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cmake -S src/px_workspace/demo -B "%DEMO_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%DEMO_VCPKG%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_INSTALLED_DIR="%DEMO_VCPKG%/installed" -DVCPKG_TARGET_TRIPLET=x64-windows-static-release
if errorlevel 1 exit /b %errorlevel%
cmake --build "%DEMO_BUILD_DIR%" --target workspace_ui_demo --parallel 4
exit /b %errorlevel%
