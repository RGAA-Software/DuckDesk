@echo off
setlocal
call "%~dp0scripts\build_cpp_target.bat" test_client_plugin_event_lifecycle
exit /b %errorlevel%
