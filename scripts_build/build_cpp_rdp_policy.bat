@echo off
setlocal
rem Focused GammaRay-owned policy build. Does not modify or rebuild upstream FreeRDP.
set "RDP_POLICY_ROOT=%~dp0.."
set "RDP_POLICY_SDK=%RDP_POLICY_ROOT%\.cache\rdp_sdk"
if not "%~1"=="" set "RDP_POLICY_SDK=%~1"
set "RDP_POLICY_HEADERS=%RDP_POLICY_SDK%"
if not "%~2"=="" set "RDP_POLICY_HEADERS=%~2"
set "RDP_POLICY_BUILD=%RDP_POLICY_ROOT%\.cache\rdp_policy_build"
if not "%~3"=="" set "RDP_POLICY_BUILD=%~3"
cmake -S "%RDP_POLICY_ROOT%\src\px_deps\px_rdp\proxy_policy" -B "%RDP_POLICY_BUILD%" -G "Visual Studio 17 2022" -A x64 -DPX_FREERDP_SOURCE_DIR="%RDP_POLICY_SDK%" -DPX_FREERDP_BINARY_DIR="%RDP_POLICY_HEADERS%"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%RDP_POLICY_BUILD%" --config Release --target rdp_proxy_policy --parallel 4
exit /b %errorlevel%
