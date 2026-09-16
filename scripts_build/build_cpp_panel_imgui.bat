@echo off
setlocal
call "%~dp0build_cpp_panel.bat" %*
exit /b %errorlevel%
