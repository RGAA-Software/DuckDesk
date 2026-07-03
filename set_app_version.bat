@echo off
setlocal
python "%~dp0set_app_version.py" %*
exit /b %errorlevel%
