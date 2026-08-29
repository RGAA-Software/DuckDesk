@echo off
setlocal

rem Focused live-pusher runtime/plugin tests. This does not build Rust,
rem web assets, or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_live_pusher_runtime test_live_pusher_ffmpeg test_live_pusher_plugin_dll_lifecycle live_pusher
exit /b %errorlevel%
