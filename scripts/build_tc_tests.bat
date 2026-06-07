@echo off
echo [BAT] Starting VsDevCmd...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
echo [BAT] VsDevCmd done, entering build dir...
cd /d d:\thunder_cloud\GammaRayPremium\build_official
echo [BAT] Now in %CD%
ninja -j8 test_file test_file_util test_folder_util test_string_util test_auto_start test_win_helper test_dxgi_mon_detector test_network_adapter test_qr_generator > build_tc_tests2.log 2>&1
echo BUILD_EXIT=%ERRORLEVEL%
