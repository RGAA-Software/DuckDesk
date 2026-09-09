@echo off
setlocal
rem Focused RDP regression tests. Does not deploy, bump versions or mutate product data.
set "RDP_REPO=%~dp0.."
set "RDP_VS="
for /f "usebackq delims=" %%V in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RDP_VS=%%V"
if not defined RDP_VS exit /b 2
call "%RDP_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=%RDP_REPO%\tools\nasm;%PATH%"
set "PROTOC=%RDP_REPO%\tools\protoc.exe"
set "CMAKE_GENERATOR=Ninja"
set "AWS_LC_SYS_C_STD=11"
set "GOOS=windows"
if exist "%ProgramFiles%\Git\usr\bin\perl.exe" set "PATH=%PATH%;%ProgramFiles%\Git\usr\bin"
cd /d "%RDP_REPO%\rust_client" || exit /b 2
cargo test --release -p service_core rdp
if errorlevel 1 exit /b %errorlevel%
cargo test --release -p px_service service_host::tests
if errorlevel 1 exit /b %errorlevel%
cargo test --release -p px_service rdp_authorization::tests
if errorlevel 1 exit /b %errorlevel%
cd /d "%RDP_REPO%\rust_server" || exit /b 2
cargo test --release -p px_console_server app_schedule::
if errorlevel 1 exit /b %errorlevel%
cargo test --release -p px_console_server connection_ticket::
exit /b %errorlevel%
