@echo off
setlocal enabledelayedexpansion

rem Incremental C++ target builder. This script never bumps the product version,
rem runs npm, invokes Cargo, collects the complete dist tree, or builds servers.
rem Usage: scripts\build_cpp_target.bat target [target ...]
rem Required environment: CPP_PRODUCT. The build directory is always isolated
rem at build_official\<product>\cmake unless a product wrapper supplies the same path.

cd /d "%~dp0\.."
if "%~1"=="" (
    echo ERROR: at least one CMake target is required.
    echo Usage: scripts\build_cpp_target.bat target [target ...]
    exit /b 2
)

if not defined CPP_PRODUCT (
    echo ERROR: CPP_PRODUCT is required. Use a scripts_build\build_cpp_product_*.bat entry point.
    exit /b 2
)
if /I not "%CPP_PRODUCT%"=="cloud_node" if /I not "%CPP_PRODUCT%"=="client" if /I not "%CPP_PRODUCT%"=="remote" (
    echo ERROR: CPP_PRODUCT must be cloud_node, client, or remote.
    exit /b 2
)
pwsh.exe -NoProfile -File "%~dp0check_cpp_readable_names.ps1"
if errorlevel 1 exit /b %errorlevel%
set "EXPECTED_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
set "BUILD_DIR=%CPP_BUILD_DIR%"
if not defined BUILD_DIR set "BUILD_DIR=%EXPECTED_BUILD_DIR%"
if /I not "%BUILD_DIR%"=="%EXPECTED_BUILD_DIR%" (
    echo ERROR: product build directory must be %EXPECTED_BUILD_DIR%; got %BUILD_DIR%.
    exit /b 2
)
set "BUILD_JOBS=%CPP_BUILD_JOBS%"
if not defined BUILD_JOBS set "BUILD_JOBS=18"

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

rem VsDevCmd may replace VCPKG_ROOT with Visual Studio's private vcpkg tree.
rem Cargo build scripts must use the repository-pinned protoc regardless of that
rem ambient mutation, including when Rust targets are launched by Ninja.
set "PROTOC=%CD%\tools\protoc.exe"
if not exist "%PROTOC%" (
    echo ERROR: repository protoc is missing: %PROTOC%
    exit /b 1
)

if exist "%BUILD_DIR%\build.ninja" if defined CPP_PRODUCT (
    findstr.exe /x /c:"PX_PRODUCT:STRING=%CPP_PRODUCT%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo ERROR: %BUILD_DIR% is not configured for PX_PRODUCT=%CPP_PRODUCT%.
        exit /b 1
    )
)

if not exist "%BUILD_DIR%\build.ninja" (
    echo C++ build tree does not exist; configuring CMake only: %CD%\%BUILD_DIR%
    if defined CPP_PRODUCT (
        cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official -DPX_PRODUCT=%CPP_PRODUCT% -Wno-dev
    ) else (
        cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official -Wno-dev
    )
    if errorlevel 1 exit /b %errorlevel%
)

echo Incremental C++ build only. Build dir: %BUILD_DIR%, jobs: %BUILD_JOBS%
echo Targets: %*
cmake --build "%BUILD_DIR%" --parallel %BUILD_JOBS% --target %*
if errorlevel 1 exit /b %errorlevel%

echo DONE: %*
endlocal
