@echo off
setlocal

set REPO_ROOT=%~dp0..
set TEST_DIR=%REPO_ROOT%\build_official\src\gr_deps\tc_common_new\tests
set LOG=%REPO_ROOT%\build_official\run_tests4.log

echo. > "%LOG%"

for %%t in (
    test_string_util
    test_file
    test_file_util
    test_folder_util
    test_auto_start
    test_win_helper
    test_dxgi_mon_detector
    test_network_adapter
    test_qr_generator
    test_process
) do (
    echo ===== %%t ===== >> "%LOG%" 2>&1
    if exist "%TEST_DIR%\%%t.exe" (
        "%TEST_DIR%\%%t.exe" >> "%LOG%" 2>&1
    ) else (
        echo [ERROR] %%t.exe not found in %TEST_DIR% >> "%LOG%" 2>&1
    )
)

echo DONE >> "%LOG%"
echo [BAT] Test log written to: %LOG%
