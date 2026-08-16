cmake_minimum_required(VERSION 3.22.1)

include(env_settings.cmake)
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(ANDROID_ABI STREQUAL "x86")
    set(VCPKG_TARGET_TRIPLET "x86-android")
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(VCPKG_TARGET_TRIPLET "x64-android")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(VCPKG_TARGET_TRIPLET "arm-neon-android")
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    set(VCPKG_TARGET_TRIPLET "arm64-android")
endif()


string(REPLACE "\\" "/" CMAKE_ANDROID_NDK "${CMAKE_ANDROID_NDK}")
message("=== ===" ${CMAKE_ANDROID_NDK})

project("client")

# px_* source modules live in GammaRayPremium/src/px_deps (inlined, no submodule).
# This CMakeLists sits at src/px_android/app/src/main/cpp/.
set(PX_PROJECT_PATH ${CMAKE_SOURCE_DIR}/../../../../../px_deps)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (ANDROID)
    add_compile_options(-Wno-error=deprecated-declarations)
    add_compile_options(-Wno-error=missing-template-arg-list-after-template-kw)
    add_compile_options(-Wno-error=deprecated-literal-operator)
    #add_compile_options(-Wno-deprecated-literal-operator)
    add_compile_definitions(ASIO2_ENABLE_SSL)
    # Vendored asio2 fmt 9.1.0's consteval format-string checking breaks with
    # NDK 29 (clang 21). Disable it; fmt falls back to runtime checking.
    add_compile_definitions(FMT_CONSTEVAL=)
    add_definitions(-DFMT_HEADER_ONLY)
endif ()
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${PX_PROJECT_PATH})
include_directories(${PX_PROJECT_PATH}/px_3rdparty)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/eventbus/src)
include_directories(${PX_PROJECT_PATH}/px_common_new)
include_directories(${PX_PROJECT_PATH}/px_client_sdk_new)
include_directories(${PX_PROJECT_PATH}/px_message_new)
include_directories(${PX_PROJECT_PATH}/px_relay_client)
include_directories(${PX_PROJECT_PATH}/px_server_protocol)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/include)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/asio)
include_directories(${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include)
include_directories(${CMAKE_BINARY_DIR}/px_message_new)
include_directories(${CMAKE_BINARY_DIR}/px_server_protocol)

set(ANDROID_OPENSSL_LIBS
    ${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/openssl/prebuilt/android/${ANDROID_ABI}/libcrypto.a
    ${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/openssl/prebuilt/android/${ANDROID_ABI}/libssl.a
)
message("===> ${ANDROID_OPENSSL_LIBS}")

# vcpkg packages that the px_* modules link but do not find_package themselves
# (resolved by the desktop root CMakeLists; replicate here for Android).
find_package(Opus CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)

# Source-depend on the inlined px_* modules (out-of-tree subdirectories).
# px_3rdparty defines Dexode::EventBus (via add_subdirectory(eventbus)).
add_subdirectory(${PX_PROJECT_PATH}/px_3rdparty ${CMAKE_BINARY_DIR}/px_3rdparty)
add_subdirectory(${PX_PROJECT_PATH}/px_common_new ${CMAKE_BINARY_DIR}/px_common_new)
add_subdirectory(${PX_PROJECT_PATH}/px_message_new ${CMAKE_BINARY_DIR}/px_message_new)
add_subdirectory(${PX_PROJECT_PATH}/px_opus_codec_new ${CMAKE_BINARY_DIR}/px_opus_codec_new)
add_subdirectory(${PX_PROJECT_PATH}/px_server_protocol ${CMAKE_BINARY_DIR}/px_server_protocol)
add_subdirectory(${PX_PROJECT_PATH}/px_relay_client ${CMAKE_BINARY_DIR}/px_relay_client)
add_subdirectory(${PX_PROJECT_PATH}/px_client_sdk_new ${CMAKE_BINARY_DIR}/px_client_sdk_new)

find_package(oboe REQUIRED CONFIG)

add_library(${CMAKE_PROJECT_NAME} SHARED
        main.cpp
        application.cpp
        frame_render.cpp
        app_context.cpp
        audio_player.cpp
        native_msg_maker.cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        #GLESv2
        GLESv3
        EGL
        android
        log
        mediandk
        oboe::oboe
        px_message
        px_common_new
        px_sdk)
