@echo off
setlocal
cd /d "%~dp0\.."
set "DEMO_VCPKG=%VCPKG_ROOT%"
if not defined DEMO_VCPKG set "DEMO_VCPKG=C:\source\vcpkg"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DEMO_VS=%%I"
if not defined DEMO_VS exit /b 1
call "%DEMO_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cmake -S src/px_workspace/demo -B build_workspace_demo -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%DEMO_VCPKG%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_INSTALLED_DIR="%DEMO_VCPKG%/installed" -DVCPKG_TARGET_TRIPLET=x64-windows-static-release
if errorlevel 1 exit /b %errorlevel%
cmake --build build_workspace_demo --target workspace_ui_demo --parallel 4
exit /b %errorlevel%
