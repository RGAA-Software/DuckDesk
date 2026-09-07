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

echo Building px_client and native runtime dependencies in %BUILD_DIR% with %PARALLEL% jobs...
cmake --build "%BUILD_DIR%" --config RelWithDebInfo --parallel %PARALLEL% --target px_client
if errorlevel 1 exit /b %errorlevel%

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\publish_cpp_artifacts.ps1 -Component client -BuildDir "%BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

echo DONE: published and hash-verified Native Client runtime artifacts.
endlocal
