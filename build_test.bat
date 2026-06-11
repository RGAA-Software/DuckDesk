@echo off
echo Starting build...
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
echo VsDevCmd done, errorlevel=%errorlevel%
cd /d D:\thunder_cloud\GammaRayPremium\build_official
echo Running ninja...
ninja test_file test_folder_util
echo Ninja done, errorlevel=%errorlevel%
