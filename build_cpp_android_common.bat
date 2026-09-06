@echo off
setlocal enabledelayedexpansion

rem Focused Android C++ build for px_common and optional dependent targets.
rem Run Gradle's native model generation once if the .cxx build tree is absent.
rem Usage: build_cpp_android_common.bat [target ...]

cd /d "%~dp0"
set "ANDROID_NATIVE_DIR="
set "ANDROID_TARGETS=%*"
if not defined ANDROID_TARGETS set "ANDROID_TARGETS=px_common"
for /f "delims=" %%D in ('where /r "src\px_android\core-native\.cxx" build.ninja 2^>nul') do (
    echo %%D | findstr /i /c:"\arm64-v8a\build.ninja" >nul
    if not errorlevel 1 if not defined ANDROID_NATIVE_DIR (
        set "ANDROID_NATIVE_DIR=%%~dpD"
        if "!ANDROID_NATIVE_DIR:~-1!"=="\" set "ANDROID_NATIVE_DIR=!ANDROID_NATIVE_DIR:~0,-1!"
    )
)

if not defined ANDROID_NATIVE_DIR (
    echo ERROR: Android native build tree is missing.
    echo Run src\px_android\gradlew.bat :core-native:generateJsonModelDebug once, then retry.
    exit /b 2
)

echo Incremental Android C++ build only. Build dir: !ANDROID_NATIVE_DIR!
echo Targets: !ANDROID_TARGETS!
cmake --build "!ANDROID_NATIVE_DIR!" --parallel 8 --target !ANDROID_TARGETS!
if errorlevel 1 exit /b %errorlevel%

echo DONE: Android !ANDROID_TARGETS!
endlocal
