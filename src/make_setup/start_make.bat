@echo off

for /d %%d in (release_*) do rmdir /s /q "%%d"

python install.py

python make_setup.py

python save_pdb.py

rem binarycreator.exe -c config/config.xml -p packages GammaRaySetup.exe -v