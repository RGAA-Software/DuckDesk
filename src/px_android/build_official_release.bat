@echo off
setlocal

rem ============================================================================
rem  Build the official release APK of the Android client (arm64-v8a).
rem
rem  Prerequisites:
rem    - Android SDK  (see local.properties: sdk.dir=D:\android\sdk)
rem    - NDK 29       (ndkVersion 29.0.14206865 in app/build.gradle)
rem    - JDK 17+      (Gradle 8.x requires JDK 17+)
rem    - vcpkg android packages installed under C:/source/vcpkg
rem      (VCPKG_ROOT is set in app/src/main/cpp/env_settings.cmake)
rem
rem  Output APK:
rem    app/build/outputs/apk/official/release/gammaray_official_<version>.apk
rem    (also copied to app/apk/release/ by the afterEvaluate hook)
rem ============================================================================

rem Prefer JDK 17 if present, so Gradle does not pick up an older JDK from PATH.
if exist "C:\Program Files\Java\jdk-17\bin\java.exe" (
    set "JAVA_HOME=C:\Program Files\Java\jdk-17"
    set "PATH=%JAVA_HOME%\bin;%PATH%"
)

echo [build_official_release] Building official release APK (arm64-v8a)...
call gradlew.bat assembleOfficialRelease --stacktrace
if errorlevel 1 (
    echo.
    echo [build_official_release] BUILD FAILED.
    exit /b 1
)

echo.
echo [build_official_release] BUILD SUCCESSFUL.
echo [build_official_release] APK(s):
dir /b "app\build\outputs\apk\official\release\*.apk" 2>nul
echo [build_official_release] Copied APK(s):
dir /b "app\apk\release\*.apk" 2>nul

endlocal
