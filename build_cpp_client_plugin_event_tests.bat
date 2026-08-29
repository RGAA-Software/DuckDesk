@echo off
setlocal
call "%~dp0scripts\build_cpp_target.bat" test_client_plugin_event_lifecycle test_client_record_plugin_dll_lifecycle media_record_client
exit /b %errorlevel%
