@echo off
setlocal

rem Focused built-in media recorder lifecycle/remux tests. This does not build
rem Rust, web assets, bump release versions, or produce a recorder plug-in DLL.
call "%~dp0scripts\build_cpp_target.bat" test_record_writer test_media_recorder_sink
exit /b %errorlevel%
