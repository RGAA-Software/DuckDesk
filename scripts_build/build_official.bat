@echo off
setlocal

rem Release-only dispatcher. A product is mandatory so one invocation can
rem consume exactly one independent product version.
if /I "%~1"=="cloud_node" (
    call "%~dp0build_cloud_node.bat" "%~2"
    exit /b %errorlevel%
)
if /I "%~1"=="client" (
    call "%~dp0build_client_product.bat" "%~2"
    exit /b %errorlevel%
)
if /I "%~1"=="remote" (
    call "%~dp0build_remote_product.bat" "%~2"
    exit /b %errorlevel%
)

echo Usage: %~nx0 ^<cloud_node^|client^|remote^> [full^|reconfigure]
exit /b 2
