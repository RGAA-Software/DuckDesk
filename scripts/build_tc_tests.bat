@echo off
setlocal
call "%~dp0..\scripts_build\build_official_tests.bat" %*
exit /b %errorlevel%
