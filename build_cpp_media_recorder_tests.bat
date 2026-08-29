@echo off
setlocal

rem Focused media recorder lifecycle/remux tests. This does not build Rust,
rem web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_record_writer test_media_recorder_runtime test_media_recorder_plugin_dll_lifecycle media_recorder
exit /b %errorlevel%
