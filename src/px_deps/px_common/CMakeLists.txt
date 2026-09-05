cmake_minimum_required(VERSION 3.22)
project(px_common)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(cpr CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)

if(WIN32)
    find_package(OpenSSL REQUIRED)
    if(FFTW3_ENABLED)
        find_package(FFTW3 CONFIG REQUIRED)
    endif()
endif()

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if(WIN32)
    add_definitions("-DUNICODE" "-D_UNICODE")
elseif(ANDROID)
    add_compile_options(-Wno-error=deprecated-declarations)
endif()

function(px_add_common_component target)
    add_library(${target} STATIC ${ARGN})
    target_compile_features(${target} PUBLIC cxx_std_23)
    target_include_directories(${target} PUBLIC
        ${PX_PROJECT_PATH}/px_3rdparty
    )
endfunction()

px_add_common_component(px_common_core
    data.cpp
    string_util.cpp
    path_codec.cpp
    time_util.cpp
    base64.cpp
    image.cpp
    num_formatter.cpp
    url_helper.cpp
)
target_link_libraries(px_common_core PUBLIC fmt::fmt spdlog::spdlog_header_only)

px_add_common_component(px_common_async
    thread.cpp
    blocking_executor.cpp
    async_runtime.cpp
    async_delay.cpp
    async_scope_drain.cpp
    callback_quiescence.cpp
    connection_attempt_workflow.cpp
    reconnect_backoff.cpp
    reconnect_supervisor.cpp
    file_transfer_route_registry.cpp
    message_notifier.cpp
    memory_stat.cpp
)
target_link_libraries(px_common_async PUBLIC px_common_core px::asio2)

px_add_common_component(px_common_file
    file.cpp
    async_file.cpp
    file_util.cpp
    folder_util.cpp
    zip_util.cpp
    clipboard/clipboard_echo.cpp
    clipboard/clipboard_file_builder.cpp
)
target_link_libraries(px_common_file PUBLIC px_common_core px_common_async miniz::miniz)

if(NOT WIN32)
    target_sources(px_common_file PRIVATE
        clipboard/clipboard_platform.cpp
        clipboard/stub/clipboard_platform_stub.cpp
    )
endif()

px_add_common_component(px_common_net
    http_client.cpp
    base_handler.cpp
    http_base_op.cpp
)
target_link_libraries(px_common_net PUBLIC px_common_core px_common_async px_common_file px::asio2 cpr::cpr)

px_add_common_component(px_common_storage shared_preference.cpp)
target_link_libraries(px_common_storage PUBLIC px_common_core leveldb::leveldb)

px_add_common_component(px_common_crypto
    md5.cpp
    px_aes.cpp
    reedsolomon/rs.c
)
target_link_libraries(px_common_crypto PUBLIC px_common_core)
if(WIN32)
    target_link_libraries(px_common_crypto PUBLIC OpenSSL::SSL OpenSSL::Crypto)
elseif(ANDROID)
    target_link_libraries(px_common_crypto PUBLIC ${ANDROID_OPENSSL_LIBS})
endif()

set(PX_COMMON_COMPONENTS
    px_common_core
    px_common_async
    px_common_file
    px_common_net
    px_common_storage
    px_common_crypto
)

if(WIN32)
    px_add_common_component(px_common_win
        dump_helper.cpp
        key_helper.cpp
        cpu_frequency.cpp
        qrcode/qr_generator.cpp
        qrcode/qrcodegen.cpp
        win32/process_helper.cpp
        win32/win_helper.cpp
        win32/audio_device_helper.cpp
        win32/firewall_helper.cpp
        win32/dxgi_mon_detector.cpp
        win32/process_cmdline.cpp
        clipboard/clipboard_platform.cpp
        clipboard/win/clipboard_platform_win.cpp
        hardware.cpp
        ip_util.cpp
        auto_start.cpp
        process_util.cpp
    )
    target_link_libraries(px_common_win PUBLIC
        px_common_core
        px_common_async
        px_common_file
        px_common_storage
        DXGI
        D3D11
        OpenSSL::SSL
        OpenSSL::Crypto
        Shlwapi.lib
        Wtsapi32.lib
        shell32.lib
        iphlpapi.lib
        advapi32.lib
        comsuppw.lib
        UserEnv.lib
        winmm.lib
        ole32.lib
        unofficial::breakpad::libbreakpad
        unofficial::breakpad::libbreakpad_client
    )
    if(DirectXTex_ENABLED)
        target_sources(px_common_win PRIVATE win32/d3d_debug_helper.cpp)
        target_link_libraries(px_common_win PUBLIC Microsoft::DirectXTex)
    endif()
    if(FFTW3_ENABLED)
        target_sources(px_common_win PRIVATE fft_32.cpp)
        target_link_libraries(px_common_win PUBLIC FFTW3::fftw3)
    endif()
    list(APPEND PX_COMMON_COMPONENTS px_common_win)
endif()

add_library(px_common STATIC common_aggregate.cpp)
target_compile_features(px_common PUBLIC cxx_std_23)
target_link_libraries(px_common PUBLIC ${PX_COMMON_COMPONENTS})

if(MSVC)
    target_sources(px_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/windows_utf8.manifest")
endif()

if(WIN32 AND TESTS_ENABLED)
    add_subdirectory(tests)
endif()
