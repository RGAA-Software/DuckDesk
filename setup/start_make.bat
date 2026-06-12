@echo off

if "%~1"=="" (
    set "BUILD_DIR=..\build_official"
) else (
    set "BUILD_DIR=%~1"
)

python make_setup.py --build-dir "%BUILD_DIR%"

python save_pdb.py --build-dir "%BUILD_DIR%"

rem binarycreator.exe -c config/config.xml -p packages GammaRaySetup.exe -v
