@echo off
setlocal enabledelayedexpansion

set REPO_ROOT=%~dp0..
set TEST_DIR=%REPO_ROOT%\build_official\src\px_deps\px_common\tests
set FT_TEST_DIR=%REPO_ROOT%\build_official\src\px_deps\px_ft_engine\tests
set RECORD_TEST_DIR=%REPO_ROOT%\build_official\src\px_deps\px_media_record\tests
set VOICE_TEST_DIR=%REPO_ROOT%\build_official\src\px_deps\px_voice_call
set CLIENT_TEST_DIR=%REPO_ROOT%\build_official\src\px_client
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
    test_thread_exit
    test_blocking_executor
    test_asio_event_dispatcher_concurrency
    test_message_notifier
    test_file_transfer_send_result
    test_async_runtime
    test_file_transfer_route_registry
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
    test_ft_async_session
    test_ft_send_contract
    test_ft_two_phase_send
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

for %%t in (
    test_record_writer
) do (
    echo ===== %%t ===== >> "%LOG%" 2>&1
    if exist "%RECORD_TEST_DIR%\%%t.exe" (
        "%RECORD_TEST_DIR%\%%t.exe" >> "%LOG%" 2>&1
        if errorlevel 1 (
            echo [FAIL] %%t >> "%LOG%" 2>&1
            set FAILED=1
        )
    ) else (
        echo [ERROR] %%t.exe not found in %RECORD_TEST_DIR% >> "%LOG%" 2>&1
        set FAILED=1
    )
)

for %%t in (
    test_voice_call
) do (
    echo ===== %%t ===== >> "%LOG%" 2>&1
    if exist "%VOICE_TEST_DIR%\%%t.exe" (
        "%VOICE_TEST_DIR%\%%t.exe" >> "%LOG%" 2>&1
        if errorlevel 1 (
            echo [FAIL] %%t >> "%LOG%" 2>&1
            set FAILED=1
        )
    ) else (
        echo [ERROR] %%t.exe not found in %VOICE_TEST_DIR% >> "%LOG%" 2>&1
        set FAILED=1
    )
)

for %%t in (
    test_client_voice_call_protocol
    test_client_virtual_display
    test_client_latest_frame_dispatch_queue
) do (
    echo ===== %%t ===== >> "%LOG%" 2>&1
    if exist "%CLIENT_TEST_DIR%\%%t.exe" (
        "%CLIENT_TEST_DIR%\%%t.exe" >> "%LOG%" 2>&1
        if errorlevel 1 (
            echo [FAIL] %%t >> "%LOG%" 2>&1
            set FAILED=1
        )
    ) else (
        echo [ERROR] %%t.exe not found in %CLIENT_TEST_DIR% >> "%LOG%" 2>&1
        set FAILED=1
    )
)

echo DONE >> "%LOG%"
echo [BAT] Test log written to: %LOG%

if !FAILED! neq 0 exit /b 1
endlocal
