@echo off
setlocal enabledelayedexpansion

set REPO_ROOT=%~dp0..
set TEST_DIR=%REPO_ROOT%\build_official\src\px_deps\px_common_new\tests
set FT_TEST_DIR=%REPO_ROOT%\build_official\src\px_deps\px_ft_engine\tests
set LOG=%REPO_ROOT%\build_official\run_tests.log
set FAILED=0

echo. > "%LOG%"

for %%t in (
    test_string_util
    test_file_util
    test_folder_util
    test_file
    test_auto_start
    test_win_helper
    test_dxgi_mon_detector
    test_network_adapter
    test_qr_generator
    test_process
    test_uncovered
    test_snowflake_id
    test_clipboard_echo
    test_clipboard_file_builder
    test_clipboard_platform
    test_px_udp_protocol
    test_common
    test_http
    test_cpu
) do (
    echo ===== %%t ===== >> "%LOG%" 2>&1
    if exist "%TEST_DIR%\%%t.exe" (
        "%TEST_DIR%\%%t.exe" >> "%LOG%" 2>&1
        if errorlevel 1 (
            echo [FAIL] %%t >> "%LOG%" 2>&1
            set FAILED=1
        )
    ) else (
        echo [ERROR] %%t.exe not found in %TEST_DIR% >> "%LOG%" 2>&1
        set FAILED=1
    )
)

for %%t in (
    test_ft_path_security
    test_ft_compress
    test_ft_transfer_job
    test_ft_engine
) do (
    echo ===== %%t ===== >> "%LOG%" 2>&1
    if exist "%FT_TEST_DIR%\%%t.exe" (
        "%FT_TEST_DIR%\%%t.exe" >> "%LOG%" 2>&1
        if errorlevel 1 (
            echo [FAIL] %%t >> "%LOG%" 2>&1
            set FAILED=1
        )
    ) else (
        echo [ERROR] %%t.exe not found in %FT_TEST_DIR% >> "%LOG%" 2>&1
        set FAILED=1
    )
)

echo DONE >> "%LOG%"
echo [BAT] Test log written to: %LOG%

if !FAILED! neq 0 exit /b 1
endlocal
