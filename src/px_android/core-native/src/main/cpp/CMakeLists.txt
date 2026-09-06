cmake_minimum_required(VERSION 3.26)

if(NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "")
    message(FATAL_ERROR "VCPKG_ROOT must point to an Android-enabled vcpkg installation")
endif()

set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "")

project(pixels_android_core LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PX_RTC_TRANSPORT_AVAILABLE OFF)
add_compile_definitions(ASIO2_ENABLE_SSL FMT_CONSTEVAL= FMT_HEADER_ONLY PX_RTC_TRANSPORT_AVAILABLE=0)
add_compile_options(
    -femulated-tls
    -Wno-error=deprecated-declarations
    -Wno-error=missing-template-arg-list-after-template-kw
    -Wno-error=deprecated-literal-operator
)

set(PX_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../px_deps")
set(PX_PROJECT_BINARY_PATH "${CMAKE_BINARY_DIR}")

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PX_PROJECT_PATH}"
    "${PX_PROJECT_PATH}/px_3rdparty"
    "${PX_PROJECT_PATH}/px_common"
    "${PX_PROJECT_PATH}/px_client_sdk"
    "${PX_PROJECT_PATH}/px_ft_engine"
    "${PX_PROJECT_PATH}/px_message"
    "${PX_PROJECT_PATH}/px_media_record"
    "${PX_PROJECT_PATH}/px_relay_client"
    "${PX_PROJECT_PATH}/px_server_protocol"
    "${PX_PROJECT_PATH}/px_3rdparty/asio2/include"
    "${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd"
    "${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/asio"
    "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include"
    "${CMAKE_BINARY_DIR}/px_message"
    "${CMAKE_BINARY_DIR}/px_server_protocol"
)

set(ANDROID_OPENSSL_LIBS
    "${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/openssl/prebuilt/android/${ANDROID_ABI}/libcrypto.a"
    "${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/openssl/prebuilt/android/${ANDROID_ABI}/libssl.a"
)

find_package(Opus CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
set(FFMPEG_ROOT "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")

add_subdirectory("${PX_PROJECT_PATH}/px_3rdparty" "${CMAKE_BINARY_DIR}/px_3rdparty")
add_subdirectory("${PX_PROJECT_PATH}/px_common" "${CMAKE_BINARY_DIR}/px_common")
add_subdirectory("${PX_PROJECT_PATH}/px_message" "${CMAKE_BINARY_DIR}/px_message")
add_subdirectory("${PX_PROJECT_PATH}/px_ft_engine" "${CMAKE_BINARY_DIR}/px_ft_engine")
add_subdirectory("${PX_PROJECT_PATH}/px_media_record" "${CMAKE_BINARY_DIR}/px_media_record")
add_subdirectory("${PX_PROJECT_PATH}/px_opus_codec" "${CMAKE_BINARY_DIR}/px_opus_codec")
add_subdirectory("${PX_PROJECT_PATH}/px_server_protocol" "${CMAKE_BINARY_DIR}/px_server_protocol")
add_subdirectory("${PX_PROJECT_PATH}/px_relay_client" "${CMAKE_BINARY_DIR}/px_relay_client")
add_subdirectory("${PX_PROJECT_PATH}/px_client_sdk" "${CMAKE_BINARY_DIR}/px_client_sdk")

add_library(pixels_android_core SHARED
    jni_adapter.cpp
    native_audio_player.cpp
    native_session.cpp
)

target_link_libraries(pixels_android_core PRIVATE
    aaudio
    android
    log
    mediandk
    px_common
    px_message
    px_sdk
    px_ft_engine
    px_media_record
)
