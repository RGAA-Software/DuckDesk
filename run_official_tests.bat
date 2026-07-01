@echo off
setlocal
call "%~dp0scripts\run_tc_tests.bat"
exit /b %errorlevel%
