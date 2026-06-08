@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cd /d d:\thunder_cloud\GammaRayPremium\build_official

echo ===== test_string_util ===== > run_tests4.log 2>&1
src\GammaRay\test_string_util.exe >> run_tests4.log 2>&1

echo ===== test_file ===== >> run_tests4.log 2>&1
src\GammaRay\test_file.exe >> run_tests4.log 2>&1

echo ===== test_file_util ===== >> run_tests4.log 2>&1
src\GammaRay\test_file_util.exe >> run_tests4.log 2>&1

echo ===== test_folder_util ===== >> run_tests4.log 2>&1
src\GammaRay\test_folder_util.exe >> run_tests4.log 2>&1

echo ===== test_auto_start ===== >> run_tests4.log 2>&1
src\GammaRay\test_auto_start.exe >> run_tests4.log 2>&1

echo ===== test_win_helper ===== >> run_tests4.log 2>&1
src\GammaRay\test_win_helper.exe >> run_tests4.log 2>&1

echo ===== test_dxgi_mon_detector ===== >> run_tests4.log 2>&1
src\GammaRay\test_dxgi_mon_detector.exe >> run_tests4.log 2>&1

echo ===== test_network_adapter ===== >> run_tests4.log 2>&1
src\GammaRay\test_network_adapter.exe >> run_tests4.log 2>&1

echo ===== test_qr_generator ===== >> run_tests4.log 2>&1
src\GammaRay\test_qr_generator.exe >> run_tests4.log 2>&1

echo ===== test_process ===== >> run_tests4.log 2>&1
src\GammaRay\test_process.exe >> run_tests4.log 2>&1

echo DONE >> run_tests4.log 2>&1
