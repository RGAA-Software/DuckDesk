@echo off
setlocal

rem Focused built-in live-pusher tests. This does not build Rust, web assets,
rem bump release versions, or produce a live-pusher plug-in DLL.
call "%~dp0scripts\build_cpp_target.bat" test_live_pusher_sink test_live_pusher_ffmpeg
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build_official" -R "^(live_pusher_sink|live_pusher_ffmpeg)$" --output-on-failure
exit /b %errorlevel%
