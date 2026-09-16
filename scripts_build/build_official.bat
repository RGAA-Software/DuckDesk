@echo off
setlocal

rem Release-only dispatcher. A product is mandatory so one invocation can
rem consume exactly one independent product version.
if /I "%~1"=="cloud_node" (
    if not "%~2"=="" goto :usage
    call "%~dp0build_cloud_node.bat"
    exit /b %errorlevel%
)
if /I "%~1"=="client" (
    if not "%~2"=="" goto :usage
    call "%~dp0build_client_product.bat"
    exit /b %errorlevel%
)
if /I "%~1"=="remote" (
    if not "%~2"=="" goto :usage
    call "%~dp0build_remote_product.bat"
    exit /b %errorlevel%
)

:usage
echo Usage: %~nx0 ^<cloud_node^|client^|remote^>
exit /b 2
