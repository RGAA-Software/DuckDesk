@echo off
setlocal
rem Compile the isolated Service heartbeat fixture, without running product builds or tests.
for %%R in ("%~dp0..\..") do set "RELAY_TEST_REPO=%%~fR"
set "CARGO_TARGET_DIR=%RELAY_TEST_REPO%\build_official\cloud_node\cargo\target"
set "RELAY_TEST_VS="
for /f "usebackq delims=" %%V in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RELAY_TEST_VS=%%V"
if not defined RELAY_TEST_VS exit /b 2
call "%RELAY_TEST_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=%RELAY_TEST_REPO%\tools\nasm;%PATH%"
set "PROTOC=%RELAY_TEST_REPO%\tools\protoc.exe"
set "CMAKE_GENERATOR=Ninja"
set "CARGO_PROFILE_RELEASE_OPT_LEVEL=1"
set "CARGO_PROFILE_RELEASE_INCREMENTAL=true"
set "CARGO_PROFILE_RELEASE_CODEGEN_UNITS=256"
set "AWS_LC_SYS_C_STD=11"
set "GOOS=windows"
if exist "%ProgramFiles%\Git\usr\bin\perl.exe" set "PATH=%PATH%;%ProgramFiles%\Git\usr\bin"
cd /d "%RELAY_TEST_REPO%\rust_client" || exit /b 2
cargo test --locked --release --jobs 18 -p px_service --bin px_service --no-run
exit /b %errorlevel%
