@echo off
setlocal enabledelayedexpansion
rem Focused Pixels-owned policy build. Product is mandatory and every output
rem stays under that product's isolated build directory.
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote [official^|customer^|oem [oem_id]]
    exit /b 2
)
if not "%~2"=="" if /I not "%~2"=="official" if /I not "%~2"=="customer" if /I not "%~2"=="oem" (
    echo ERROR: distribution must be official, customer, or oem when supplied.
    exit /b 2
)
if /I "%~2"=="oem" if "%~3"=="" (
    echo ERROR: OEM RDP policy builds require an OEM ID.
    exit /b 2
)
if /I "%~2"=="oem" (
    call :validate_oem_id "%~3"
    if errorlevel 1 exit /b 2
)
if /I not "%~2"=="oem" if not "%~3"=="" (
    echo ERROR: an OEM ID is accepted only for the OEM distribution.
    exit /b 2
)
set "RDP_POLICY_ROOT=%~dp0.."
set "RDP_POLICY_SDK=%RDP_POLICY_ROOT%\.cache\rdp_sdk"
set "RDP_POLICY_HEADERS=%RDP_POLICY_SDK%"
if "%~2"=="" (
    set "RDP_POLICY_BUILD=%RDP_POLICY_ROOT%\build_official\%~1\rdp_policy"
) else if /I "%~2"=="oem" (
    set "RDP_POLICY_BUILD=%RDP_POLICY_ROOT%\build_official\%~1\oem\%~3\rdp_policy"
) else (
    set "RDP_POLICY_BUILD=%RDP_POLICY_ROOT%\build_official\%~1\%~2\rdp_policy"
)
cmake -S "%RDP_POLICY_ROOT%\src\px_deps\px_rdp\proxy_policy" -B "%RDP_POLICY_BUILD%" -G "Visual Studio 17 2022" -A x64 -DPX_FREERDP_SOURCE_DIR="%RDP_POLICY_SDK%" -DPX_FREERDP_BINARY_DIR="%RDP_POLICY_HEADERS%"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%RDP_POLICY_BUILD%" --config Release --target rdp_proxy_policy --parallel 4
exit /b %errorlevel%

:validate_oem_id
set "OEM_ID_CANDIDATE=%~1"
echo(%OEM_ID_CANDIDATE%| findstr.exe /r /x "[a-z0-9][a-z0-9-]*[a-z0-9]" >nul
if errorlevel 1 (
    echo ERROR: OEM ID must be a canonical lowercase identifier.
    exit /b 2
)
echo(%OEM_ID_CANDIDATE%| findstr.exe /c:"--" >nul
if not errorlevel 1 (
    echo ERROR: OEM ID must not contain consecutive hyphens.
    exit /b 2
)
if /I "%OEM_ID_CANDIDATE%"=="pixels" exit /b 2
if /I "%OEM_ID_CANDIDATE%"=="official" exit /b 2
if /I "%OEM_ID_CANDIDATE%"=="customer" exit /b 2
if /I "%OEM_ID_CANDIDATE%"=="oem" exit /b 2
if not "%OEM_ID_CANDIDATE:~32,1%"=="" (
    echo ERROR: OEM ID must contain at most 32 characters.
    exit /b 2
)
exit /b 0
