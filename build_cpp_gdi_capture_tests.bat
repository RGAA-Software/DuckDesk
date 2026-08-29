@echo off
setlocal

rem Focused GDI capture worker and DLL lifecycle tests. This does not build
rem Rust, web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_gdi_capture_plugin_dll_lifecycle cap_gdi
exit /b %errorlevel%
