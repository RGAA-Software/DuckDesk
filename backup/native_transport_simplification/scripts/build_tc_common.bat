@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 > nul
cd /d D:\thunder_cloud\GammaRayPremium
cmake --build build_official --target test_string_util test_file test_file_util test_folder_util test_auto_start test_win_helper test_dxgi_mon_detector test_network_adapter test_qr_generator > build_tc_tests.log 2>&1
echo DONE
