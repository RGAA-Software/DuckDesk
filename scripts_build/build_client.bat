@echo off
echo build_client.bat now maps to the independent Pixels Client product build.
call "%~dp0build_client_product.bat" %*
exit /b %errorlevel%
