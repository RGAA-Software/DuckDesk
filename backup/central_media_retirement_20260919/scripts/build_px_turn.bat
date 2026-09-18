@echo off
setlocal

:: Build the bundled Coturn sidecar as a static x64 Release executable.
:: Override COTURN_ROOT or VCPKG_ROOT when the source/dependency locations differ.
cd /d "%~dp0\.."
set "REPO_ROOT=%cd%"

if not defined COTURN_ROOT set "COTURN_ROOT=%REPO_ROOT%\..\coturn"
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\source\vcpkg"
set "BUILD_DIR=%COTURN_ROOT%\build_px_turn"
set "MEDIA_DIR=%REPO_ROOT%\rust_server\px_console_server\media"

if not exist "%COTURN_ROOT%\CMakeLists.txt" (
    echo ERROR: Coturn source was not found at: %COTURN_ROOT%
    echo Set COTURN_ROOT to the Coturn source checkout and run again.
    exit /b 1
)

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: vcpkg was not found at: %VCPKG_ROOT%
    echo Set VCPKG_ROOT to a vcpkg checkout and run again.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Build Tools with the x64 C++ toolchain are required.
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_ROOT=%%I"
if not defined VS_ROOT (
    echo ERROR: No Visual Studio installation with the x64 C++ toolchain was found.
    exit /b 1
)

call "%VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

cmake -S "%COTURN_ROOT%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-release ^
    -DWITH_MYSQL=OFF ^
    -DBUILD_TESTING=OFF ^
    -DCMAKE_DISABLE_FIND_PACKAGE_PostgreSQL=TRUE
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --target turnserver --parallel
if errorlevel 1 exit /b 1

copy /Y "%BUILD_DIR%\bin\turnserver.exe" "%MEDIA_DIR%\px_turn.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to publish px_turn.exe to the Console media directory.
    exit /b 1
)

copy /Y "%COTURN_ROOT%\LICENSE" "%MEDIA_DIR%\COTURN_LICENSE" >nul
if errorlevel 1 (
    echo ERROR: Failed to publish the Coturn license.
    exit /b 1
)

echo Built and published: %MEDIA_DIR%\px_turn.exe
endlocal
