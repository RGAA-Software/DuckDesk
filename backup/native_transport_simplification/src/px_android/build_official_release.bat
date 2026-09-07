@echo off
setlocal

rem Pixels Android release-only entry point. Production signing credentials are
rem read from PIXELS_* environment variables or ignored keystore.properties.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_release.ps1" %*
set "PIXELS_BUILD_EXIT=%ERRORLEVEL%"

if not "%PIXELS_BUILD_EXIT%"=="0" (
    echo [Pixels] Release build failed.
    exit /b %PIXELS_BUILD_EXIT%
)

echo [Pixels] Signed release artifacts are ready under app\apk\release.
endlocal
