@echo off
setlocal

rem Product-specific compatibility-free entry point.
rem Usage: scripts\build_px_client.bat cloud_node^|client^|remote [parallelism]
call "%~dp0..\scripts_build\build_cpp_product_client.bat" %*
exit /b %errorlevel%
