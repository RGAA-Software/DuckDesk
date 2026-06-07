@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul 2>&1
cd /d %~dp0\build_official
ninja tc_common_new.lib > ..\build_tc.log 2>&1
echo EXIT_CODE=%ERRORLEVEL% >> ..\build_tc.log
