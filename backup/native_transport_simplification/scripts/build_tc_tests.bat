@echo off
setlocal
call "%~dp0..\build_official_tests.bat" %*
exit /b %errorlevel%
