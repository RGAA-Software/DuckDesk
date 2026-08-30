@echo off
setlocal

rem Focused Panel shutdown orchestration test and Panel build. This does not
rem build Rust/web workspaces or bump release versions.
call "%~dp0scripts\build_cpp_target.bat" test_panel_shutdown_sequence test_panel_running_pipe_lifecycle px_panel
exit /b %errorlevel%
