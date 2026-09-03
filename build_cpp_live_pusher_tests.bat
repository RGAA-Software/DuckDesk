@echo off
setlocal

rem Focused built-in live-pusher tests. This does not build Rust, web assets,
rem bump release versions, or produce a live-pusher plug-in DLL.
call "%~dp0scripts\build_cpp_target.bat" test_live_pusher_sink test_live_pusher_ffmpeg
exit /b %errorlevel%
