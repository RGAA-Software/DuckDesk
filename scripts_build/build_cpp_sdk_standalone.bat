@echo off
setlocal enabledelayedexpansion
rem SDK consumer only; never runs the application, npm, Cargo or release build.
rem Usage: build_cpp_sdk_standalone.bat [windows^|android] [full^|core]
cd /d "%~dp0.." || exit /b 1
rem CMake must decode localized MSVC /showIncludes output in the same encoding as Ninja.
chcp 65001 >nul
set "SDK_PLATFORM=%~1"
if not defined SDK_PLATFORM set "SDK_PLATFORM=windows"
set "SDK_MODE=%~2"
if not defined SDK_MODE set "SDK_MODE=full"
if not "%SDK_MODE%"=="full" if not "%SDK_MODE%"=="core" exit /b 2
set "SDK_CORE_ONLY=OFF"
if "%SDK_MODE%"=="core" set "SDK_CORE_ONLY=ON"
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:/source/vcpkg"
if not defined CPP_BUILD_JOBS set "CPP_BUILD_JOBS=8"
set "SDK_BUILD_DIR=build_sdk_%SDK_PLATFORM%_%SDK_MODE%"
set "SDK_SOURCE=src/px_client_sdk/examples/lifecycle"
set "SDK_TARGETS=%CPP_SDK_TARGETS%"
if not defined SDK_TARGETS set "SDK_TARGETS=pixels_sdk_lifecycle"

if "%SDK_PLATFORM%"=="windows" (
    set "SDK_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    for /f "usebackq delims=" %%I in (`"!SDK_VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SDK_VS=%%I"
    if not defined SDK_VS exit /b 2
    call "!SDK_VS!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
    if errorlevel 1 exit /b !errorlevel!
    set "VSLANG=1033"
    rem Use single-pass manifest embedding; no intermediate executable needs to be reopened.
    cmake -S "%SDK_SOURCE%" -B "%SDK_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO="/debug /INCREMENTAL:NO" -DCMAKE_EXE_LINKER_FLAGS_DEBUG="/debug /INCREMENTAL:NO" -DPX_SDK_CORE_ONLY=%SDK_CORE_ONLY% -DPX_SDK_BUILD_QT_TESTS=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON -DBUILD_TESTING=ON
) else if "%SDK_PLATFORM%"=="android" (
    if not defined ANDROID_NDK_HOME (
        echo ERROR: Set ANDROID_NDK_HOME to the installed Android NDK directory.
        exit /b 2
    )
    cmake -S "%SDK_SOURCE%" -B "%SDK_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="!ANDROID_NDK_HOME!/build/cmake/android.toolchain.cmake" -DVCPKG_TARGET_TRIPLET=arm64-android -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-31 -DANDROID_STL=c++_shared -DPX_SDK_CORE_ONLY=%SDK_CORE_ONLY% -DPX_SDK_BUILD_QT_TESTS=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON -DBUILD_TESTING=OFF
) else (
    echo ERROR: Unsupported SDK platform. Use windows or android.
    exit /b 2
)
if errorlevel 1 exit /b !errorlevel!
cmake --build "%SDK_BUILD_DIR%" --parallel %CPP_BUILD_JOBS% --target %SDK_TARGETS%
if errorlevel 1 exit /b !errorlevel!
if "%SDK_PLATFORM%"=="windows" (
    ctest --test-dir "%SDK_BUILD_DIR%" --output-on-failure --timeout 20 -R "^pixels_sdk_consumer_lifecycle$"
    if errorlevel 1 exit /b !errorlevel!
)
echo DONE: SDK-only %SDK_PLATFORM% %SDK_MODE% consumer.
exit /b 0
