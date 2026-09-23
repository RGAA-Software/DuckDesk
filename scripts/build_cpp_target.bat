@echo off
setlocal enabledelayedexpansion

rem Incremental C++ target builder. This script never bumps the product version,
rem runs npm, invokes Cargo, collects the complete dist tree, or builds servers.
rem Usage: scripts\build_cpp_target.bat target [target ...]
rem Required environment: CPP_PRODUCT. Focused builds use the development tree
rem at build_official\<product>\cmake. Release orchestration sets
rem CPP_DISTRIBUTION=official|customer|oem and uses a nested flavor tree.

cd /d "%~dp0\.."
rem CMake and Ninja must decode localized MSVC /showIncludes output as UTF-8.
chcp 65001 >nul
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
set "BUILD_DISTRIBUTION=%CPP_DISTRIBUTION%"
if not defined BUILD_DISTRIBUTION set "BUILD_DISTRIBUTION=development"
if /I not "%BUILD_DISTRIBUTION%"=="development" if /I not "%BUILD_DISTRIBUTION%"=="official" if /I not "%BUILD_DISTRIBUTION%"=="customer" if /I not "%BUILD_DISTRIBUTION%"=="oem" (
    echo ERROR: CPP_DISTRIBUTION must be development, official, customer, or oem.
    exit /b 2
)
if /I not "%BUILD_DISTRIBUTION%"=="oem" goto :select_build_directory
if not defined CPP_OEM_ID (
    echo ERROR: CPP_OEM_ID is required for an OEM build.
    exit /b 2
)
echo(%CPP_OEM_ID%| findstr.exe /r /x "[a-z0-9][a-z0-9-]*[a-z0-9]" >nul
if errorlevel 1 (
    echo ERROR: CPP_OEM_ID must be a canonical lowercase OEM identifier.
    exit /b 2
)
echo(%CPP_OEM_ID%| findstr.exe /c:"--" >nul
if not errorlevel 1 (
    echo ERROR: CPP_OEM_ID must not contain consecutive hyphens.
    exit /b 2
)
if /I "%CPP_OEM_ID%"=="pixels" exit /b 2
if /I "%CPP_OEM_ID%"=="official" exit /b 2
if /I "%CPP_OEM_ID%"=="customer" exit /b 2
if /I "%CPP_OEM_ID%"=="oem" exit /b 2
if not "%CPP_OEM_ID:~32,1%"=="" (
    echo ERROR: CPP_OEM_ID must contain at most 32 characters.
    exit /b 2
)

:select_build_directory
if /I "%BUILD_DISTRIBUTION%"=="development" (
    set "EXPECTED_BUILD_DIR=build_official\%CPP_PRODUCT%\cmake"
) else if /I "%BUILD_DISTRIBUTION%"=="oem" (
    set "EXPECTED_BUILD_DIR=build_official\%CPP_PRODUCT%\oem\%CPP_OEM_ID%\cmake"
) else (
    set "EXPECTED_BUILD_DIR=build_official\%CPP_PRODUCT%\%BUILD_DISTRIBUTION%\cmake"
)
set "BUILD_DIR=%CPP_BUILD_DIR%"
if not defined BUILD_DIR set "BUILD_DIR=%EXPECTED_BUILD_DIR%"
if /I not "%BUILD_DIR%"=="%EXPECTED_BUILD_DIR%" (
    echo ERROR: product build directory must be %EXPECTED_BUILD_DIR%; got %BUILD_DIR%.
    exit /b 2
)
set "BUILD_JOBS=%CPP_BUILD_JOBS%"
if not defined BUILD_JOBS set "BUILD_JOBS=18"
set "FAST_RELEASE=OFF"
if /I "%BUILD_DISTRIBUTION%"=="development" set "FAST_RELEASE=ON"

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

set "CONFIGURE_REQUIRED=0"
if not exist "%BUILD_DIR%\build.ninja" set "CONFIGURE_REQUIRED=1"
if exist "%BUILD_DIR%\build.ninja" if defined CPP_PRODUCT (
    findstr.exe /x /c:"PX_PRODUCT:STRING=%CPP_PRODUCT%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo ERROR: %BUILD_DIR% is not configured for PX_PRODUCT=%CPP_PRODUCT%.
        exit /b 1
    )
    findstr.exe /x /c:"PX_DISTRIBUTION:STRING=%BUILD_DISTRIBUTION%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo ERROR: %BUILD_DIR% is not configured for PX_DISTRIBUTION=%BUILD_DISTRIBUTION%.
        exit /b 1
    )
    findstr.exe /x /c:"CMAKE_BUILD_TYPE:STRING=Release" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 set "CONFIGURE_REQUIRED=1"
    findstr.exe /x /c:"PX_FAST_RELEASE:BOOL=%FAST_RELEASE%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 set "CONFIGURE_REQUIRED=1"
)

if "%CONFIGURE_REQUIRED%"=="1" (
    echo Configuring C++ Release tree: %CD%\%BUILD_DIR% ^(fast=%FAST_RELEASE%^)
    if defined CPP_PRODUCT (
        cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPX_FAST_RELEASE=%FAST_RELEASE% -DTARGET_TYPE=Official -DPX_PRODUCT=%CPP_PRODUCT% -DPX_DISTRIBUTION=%BUILD_DISTRIBUTION% %CPP_CMAKE_DISTRIBUTION_ARGS% -Wno-dev
    ) else (
        cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPX_FAST_RELEASE=%FAST_RELEASE% -DTARGET_TYPE=Official -Wno-dev
    )
    if errorlevel 1 exit /b !errorlevel!
)

echo Incremental C++ build only. Build dir: %BUILD_DIR%, jobs: %BUILD_JOBS%
echo Targets: %*
cmake --build "%BUILD_DIR%" --parallel %BUILD_JOBS% --target %*
if errorlevel 1 exit /b !errorlevel!

echo DONE: %*
endlocal
exit /b 0
