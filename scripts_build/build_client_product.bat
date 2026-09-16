@echo off
setlocal
cd /d "%~dp0.." || exit /b 1

if not "%~1"=="" if /I not "%~1"=="full" if /I not "%~1"=="reconfigure" (
    echo Usage: %~nx0 [full^|reconfigure]
    exit /b 2
)

where python >nul 2>&1 || (echo ERROR: python is required. & exit /b 1)
python "%~dp0..\scripts\validate_product_manifests.py"
if errorlevel 1 exit /b %errorlevel%
python "%~dp0..\scripts\validate_product_branding.py"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0build_cpp_rdp_sdk.bat"
if errorlevel 1 exit /b %errorlevel%

rem A complete Client product invocation consumes exactly one Client version.
python "%~dp0..\set_product_version.py" --product client --bump
if errorlevel 1 exit /b %errorlevel%

set "CPP_PRODUCT=client"
set "CPP_BUILD_DIR=build_official\client"
set "CPP_BUILD_JOBS=18"
call "%~dp0..\scripts\build_cpp_target.bat" px_build_client_all
if errorlevel 1 exit /b %errorlevel%
python "%~dp0..\setup\make_setup.py" --product client
exit /b %errorlevel%
