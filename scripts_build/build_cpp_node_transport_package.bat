@echo off
setlocal
rem Focused transport candidate: refresh changed C++ runtimes in an existing complete official tree.
rem Build the publication transport library and official Service separately before this entry point.
rem No version bump, npm, development-dist repackaging, or unrelated workspace build.
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote candidate-id [jobs]
    exit /b 2
)
if "%~2"=="" exit /b 2
cd /d "%~dp0.." || exit /b 1
set "CPP_PRODUCT=%~1"
set "CPP_DISTRIBUTION=official"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\official\cmake"
if not "%~3"=="" set "CPP_BUILD_JOBS=%~3"
if not exist "%CPP_BUILD_DIR%\CMakeCache.txt" exit /b 2
if not exist "rust_transport\target\publication\px_transport_ffi.lib" exit /b 2
python setup\make_setup.py --product %CPP_PRODUCT% --distribution official --validate-only
if errorlevel 1 exit /b %errorlevel%
call "%~dp0..\scripts\build_cpp_target.bat" px_render px_panel px_client
if errorlevel 1 exit /b %errorlevel%
python scripts\collect_dist.py --source-dir . --build-dir "%CPP_BUILD_DIR%" --product %CPP_PRODUCT% --distribution official --update-root-file "build_official\%CPP_PRODUCT%\official\update\update-root.json" --dist-dir "build_official\%CPP_PRODUCT%\official\dist"
if errorlevel 1 exit /b %errorlevel%
python scripts\verify_product_dist.py "build_official\%CPP_PRODUCT%\official\dist"
if errorlevel 1 exit /b %errorlevel%
python setup\make_setup.py --product %CPP_PRODUCT% --distribution official --candidate-id "%~2"
exit /b %errorlevel%
