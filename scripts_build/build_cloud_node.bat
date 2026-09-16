@echo off
setlocal
cd /d "%~dp0.." || exit /b 1

if not "%~1"=="" (
    echo Usage: %~nx0
    exit /b 2
)

where python >nul 2>&1 || (echo ERROR: python is required. & exit /b 1)
where node >nul 2>&1 || (echo ERROR: node is required. & exit /b 1)
where npm >nul 2>&1 || (echo ERROR: npm is required. & exit /b 1)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0clean_product_outputs.ps1" -Product cloud_node
if errorlevel 1 exit /b %errorlevel%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\third_party\cef\fetch_cef.ps1" >nul
if errorlevel 1 exit /b %errorlevel%

python "%~dp0..\scripts\validate_product_manifests.py"
if errorlevel 1 exit /b %errorlevel%
python "%~dp0..\scripts\validate_product_branding.py"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0build_cpp_rdp_sdk.bat"
if errorlevel 1 exit /b %errorlevel%
call "%~dp0build_cpp_rdp_policy.bat" cloud_node
if errorlevel 1 exit /b %errorlevel%

rem The product version is consumed exactly once after all static preflight checks.
python "%~dp0..\set_product_version.py" --product cloud_node --bump
if errorlevel 1 exit /b %errorlevel%

node "%~dp0..\scripts\sync_web_protos.mjs"
if errorlevel 1 exit /b %errorlevel%

pushd "%~dp0..\web\px_web_client"
call npm ci
if errorlevel 1 (popd & exit /b 1)
call npm run build -- --outDir "%~dp0..\build_official\cloud_node\web" --emptyOutDir
if errorlevel 1 (popd & exit /b 1)
popd

set "CPP_PRODUCT=cloud_node"
set "CPP_BUILD_DIR=build_official\cloud_node\cmake"
set "CPP_BUILD_JOBS=18"
call "%~dp0..\scripts\build_cpp_target.bat" px_build_cloud_node_all
if errorlevel 1 exit /b %errorlevel%
python "%~dp0..\setup\make_setup.py" --product cloud_node
exit /b %errorlevel%
