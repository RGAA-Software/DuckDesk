@echo off

if "%~1"=="" (
    set "BUILD_DIR=..\build_official"
) else (
    set "BUILD_DIR=%~1"
)

python make_setup.py --build-dir "%BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

rem PDB collection is auxiliary; do not fail packaging if it only produces warnings/logs.
python save_pdb.py --build-dir "%BUILD_DIR%" || echo Warning: save_pdb.py finished with non-zero exit code, but packaging is already complete.

rem binarycreator.exe -c config/config.xml -p packages GammaRaySetup.exe -v
exit /b 0
