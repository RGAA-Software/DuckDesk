@echo off
setlocal

rem Focused built-in media recorder lifecycle/remux tests. This does not build
rem Rust, web assets, bump release versions, or produce a recorder plug-in DLL.
call "%~dp0scripts\build_cpp_target.bat" test_record_writer test_media_recorder_sink
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build_official" -R "^(render_record_writer|media_recorder_sink)$" --output-on-failure
exit /b %errorlevel%
