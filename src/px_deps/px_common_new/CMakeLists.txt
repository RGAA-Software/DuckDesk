cmake_minimum_required(VERSION 3.22)
project(px_common_new)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#find_package(OpenSSL REQUIRED)

#if (ANDROID)
#    include(FetchContent)
#    FetchContent_Declare(cpr GIT_REPOSITORY https://github.com/libcpr/cpr.git
#            GIT_TAG 1.12.0) # Replace with your desired git commit from: https://github.com/libcpr/cpr/releases
#    set(CURL_USE_LIBPSL OFF FORCE)
#    set(ZLIB_COMPAT OFF FORCE)
#    FetchContent_MakeAvailable(cpr)
#
#endif ()
find_package(cpr CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)

if (WIN32)
    if (FFTW3_ENABLED)
        find_package(FFTW3 CONFIG REQUIRED)
    endif()
    find_package(OpenSSL REQUIRED)
    #set(VCPKG_STATIC_LIB_PATH ${VCPKG_ROOT}/installed/x64-windows-static/lib)
    set(VCPKG_STATIC_LIB_PATH )
elseif (ANDROID)
    set(VCPKG_STATIC_LIB_PATH ${VCPKG_ROOT}/installed/arm64-android/lib)
endif ()
link_directories(VCPKG_STATIC_LIB_PATH)

message("==--== " ${GR_PROJECT_PATH}/px_3rdparty/eventbus/src)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if (WIN32)
    add_definitions("-DUNICODE" "-D_UNICODE")
elseif (ANDROID)
    add_compile_options(-Wno-error=deprecated-declarations)
endif ()

set(SRCS
    data.cpp
    shared_preference.cpp
    string_util.cpp
    time_util.cpp
    thread.cpp
    task_runtime.cpp
    base64.cpp
    file.cpp
    image.cpp
    message_notifier.cpp
    http_client.cpp
    file_util.cpp
    dump_helper.cpp
    folder_util.cpp
    clipboard/clipboard_echo.cpp
    clipboard/clipboard_file_builder.cpp
    clipboard/clipboard_platform.cpp
    clipboard/stub/clipboard_platform_stub.cpp
    num_formatter.cpp
    monitors.cpp
    key_helper.cpp
    url_helper.cpp
    ws_server.cpp
    message_looper.cpp
    base_handler.cpp
    http_base_op.cpp
    math_helper.cpp
    px_aes.cpp
    memory_stat.cpp
    cpu_frequency.cpp
    gd_md5.cpp
    zip_util.cpp
    reedsolomon/rs.c
)

if(WIN32)
    list(APPEND SRCS
        win32/process_helper.cpp
        win32/win_helper.cpp
        win32/dynamic_library.cpp
        win32/audio_device_helper.cpp
        win32/firewall_helper.cpp
        qrcode/qr_generator.cpp
        qrcode/qrcodegen.cpp
        qrcode/qrcodegen.hpp
        win32/dxgi_mon_detector.cpp
        win32/process_cmdline.cpp
        clipboard/win/clipboard_platform_win.cpp
        hardware.cpp
        ip_util.cpp
        auto_start.cpp
        process_util.cpp
        qwidget_helper.cpp
    )
endif()

if(DirectXTex_ENABLED)
    list(APPEND SRCS
        win32/d3d_debug_helper.cpp
        win32/d3d_render.cc
    )
endif()

if(FFTW3_ENABLED)
    list(APPEND SRCS
        fft_32.cpp
    )
endif()

add_library(px_common_new ${SRCS})
target_include_directories(px_common_new PUBLIC ${GR_PROJECT_PATH}/px_3rdparty/)
target_include_directories(px_common_new PUBLIC ${GR_PROJECT_PATH}/px_3rdparty/openssl/x64/include)
target_include_directories(px_common_new PUBLIC ${GR_PROJECT_PATH}/px_3rdparty/eventbus/src)
target_link_libraries(px_common_new PUBLIC Dexode::EventBus)
if (WIN32)
    if(TESTS_ENABLED)
        add_subdirectory(tests)
    endif()

    target_link_libraries(px_common_new PUBLIC
        DXGI D3D11 OpenSSL::SSL OpenSSL::Crypto
        Shlwapi.lib Wtsapi32.lib shell32.lib
        iphlpapi.lib advapi32.lib comsuppw.lib
        UserEnv.lib winmm.lib
        ole32.lib
    )
    if (DirectXTex_ENABLED)
        target_link_libraries(px_common_new PUBLIC Microsoft::DirectXTex)
    endif()
    if (FFTW3_ENABLED)
        target_link_libraries(px_common_new PUBLIC FFTW3::fftw3)
    endif()
    target_link_libraries(px_common_new PUBLIC
        unofficial::breakpad::libbreakpad
        unofficial::breakpad::libbreakpad_client
    )
    # TODO: Remove Qt INTERFACE link after all dependent modules explicitly link Qt themselves
    # Qt dependencies have been fully removed from px_common_new source.
    # All downstream targets that use Qt must link it explicitly.
endif ()

if (ANDROID)
    target_link_libraries(px_common_new PUBLIC ${ANDROID_OPENSSL_LIBS})
endif ()

target_link_libraries(px_common_new PUBLIC leveldb::leveldb fmt::fmt spdlog::spdlog_header_only miniz::miniz)
if (WIN32)
    target_link_libraries(px_common_new PUBLIC cpr::cpr)
endif ()

target_include_directories(px_common_new PUBLIC ${GR_PROJECT_PATH}/px_3rdparty/eventbus/src)
