@echo off
setlocal
rem Build the user's external demo target only; no GammaRay release build or version bump.
cmake --build D:\dolit\rdp\build --config Release --target freerdp_qt_client --parallel 6
exit /b %errorlevel%
