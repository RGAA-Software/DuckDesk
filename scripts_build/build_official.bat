@echo off
setlocal

rem Release-only dispatcher. A product is mandatory so one invocation can
rem consume exactly one independent product version.
if /I "%~1"=="cloud_node" goto :cloud_node
if /I "%~1"=="client" goto :client
if /I "%~1"=="remote" goto :remote
goto :usage

:cloud_node
if not "%~2"=="" goto :usage
call "%~dp0build_cloud_node.bat"
exit /b %errorlevel%

:client
if not "%~2"=="" goto :usage
call "%~dp0build_client_product.bat"
exit /b %errorlevel%

:remote
if not "%~2"=="" goto :usage
call "%~dp0build_remote_product.bat"
exit /b %errorlevel%

:usage
echo Usage: %~nx0 ^<cloud_node^|client^|remote^>
exit /b 2
