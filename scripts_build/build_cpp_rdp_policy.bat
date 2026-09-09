@echo off
setlocal
rem Focused GammaRay-owned policy build. Does not modify or rebuild upstream FreeRDP.
if "%~1"=="" goto usage
if "%~2"=="" goto usage
set "RDP_POLICY_ROOT=%~dp0.."
set "RDP_POLICY_BUILD=%RDP_POLICY_ROOT%\.cache\rdp_policy_build"
cmake -S "%RDP_POLICY_ROOT%\src\px_deps\px_rdp\proxy_policy" -B "%RDP_POLICY_BUILD%" -G "Visual Studio 17 2022" -A x64 -DPX_FREERDP_SOURCE_DIR="%~1" -DPX_FREERDP_BINARY_DIR="%~2"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%RDP_POLICY_BUILD%" --config Release --target rdp_proxy_policy --parallel 4
exit /b %errorlevel%
:usage
echo Usage: build_cpp_rdp_policy.bat ^<pinned-FreeRDP-source-or-SDK^> ^<matching-generated-headers^>
exit /b 2
