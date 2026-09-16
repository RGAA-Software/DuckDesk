@echo off
setlocal
rem Focused Pixels-owned policy build. Product is mandatory and every output
rem stays under that product's isolated build directory.
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote
    exit /b 2
)
set "RDP_POLICY_ROOT=%~dp0.."
set "RDP_POLICY_SDK=%RDP_POLICY_ROOT%\.cache\rdp_sdk"
set "RDP_POLICY_HEADERS=%RDP_POLICY_SDK%"
set "RDP_POLICY_BUILD=%RDP_POLICY_ROOT%\build_official\%~1\rdp_policy"
cmake -S "%RDP_POLICY_ROOT%\src\px_deps\px_rdp\proxy_policy" -B "%RDP_POLICY_BUILD%" -G "Visual Studio 17 2022" -A x64 -DPX_FREERDP_SOURCE_DIR="%RDP_POLICY_SDK%" -DPX_FREERDP_BINARY_DIR="%RDP_POLICY_HEADERS%"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%RDP_POLICY_BUILD%" --config Release --target rdp_proxy_policy --parallel 4
exit /b %errorlevel%
