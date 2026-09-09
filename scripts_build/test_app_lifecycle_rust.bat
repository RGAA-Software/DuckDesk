@echo off
setlocal
rem Focused lifecycle regression tests; no deployment, configuration edits or version bump.
set "APP_TEST_REPO=%~dp0.."
set "APP_TEST_VS="
for /f "usebackq delims=" %%V in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "APP_TEST_VS=%%V"
if not defined APP_TEST_VS exit /b 2
call "%APP_TEST_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=%APP_TEST_REPO%\tools\nasm;%PATH%"
set "PROTOC=%APP_TEST_REPO%\tools\protoc.exe"
set "CMAKE_GENERATOR=Ninja"
set "AWS_LC_SYS_C_STD=11"
set "GOOS=windows"
if exist "%ProgramFiles%\Git\usr\bin\perl.exe" set "PATH=%PATH%;%ProgramFiles%\Git\usr\bin"
cd /d "%APP_TEST_REPO%\rust_client" || exit /b 2
cargo test --release -p service_core app_instance:: -- --test-threads=1
if errorlevel 1 exit /b %errorlevel%
cargo test --release -p px_service windows_process::tests
if errorlevel 1 exit /b %errorlevel%
cargo test --release -p px_service service_host::tests
if errorlevel 1 exit /b %errorlevel%
cd /d "%APP_TEST_REPO%\rust_server" || exit /b 2
cargo test --release -p px_console_server app_schedule::
exit /b %errorlevel%
