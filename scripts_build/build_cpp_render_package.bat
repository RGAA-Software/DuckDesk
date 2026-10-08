@echo off
setlocal
rem Focused Render package refresh: reuse the complete official product build.
rem No version bump, npm, Cargo workspace build, or development-dist repackaging.
rem A unique candidate ID keeps validation installers separate from published releases.
if /I not "%~1"=="cloud_node" if /I not "%~1"=="remote" (
    echo Usage: %~nx0 cloud_node^|remote candidate-id
    exit /b 2
)
if "%~2"=="" exit /b 2
cd /d "%~dp0.." || exit /b 1
set "CPP_PRODUCT=%~1"
set "CPP_DISTRIBUTION=official"
set "CPP_BUILD_DIR=build_official\%CPP_PRODUCT%\official\cmake"
if not exist "%CPP_BUILD_DIR%\CMakeCache.txt" (
    echo ERROR: an existing complete official product build is required.
    exit /b 2
)
python setup\make_setup.py --product %CPP_PRODUCT% --distribution official --validate-only
if errorlevel 1 exit /b %errorlevel%
call "%~dp0..\scripts\build_cpp_target.bat" px_render
if errorlevel 1 exit /b %errorlevel%
python scripts\collect_dist.py --source-dir . --build-dir "%CPP_BUILD_DIR%" --product %CPP_PRODUCT% --distribution official --update-root-file "build_official\%CPP_PRODUCT%\official\update\update-root.json" --dist-dir "build_official\%CPP_PRODUCT%\official\dist"
if errorlevel 1 exit /b %errorlevel%
python setup\make_setup.py --product %CPP_PRODUCT% --distribution official --candidate-id "%~2"
exit /b %errorlevel%
