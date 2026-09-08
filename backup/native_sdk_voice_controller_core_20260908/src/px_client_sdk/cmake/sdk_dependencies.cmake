# Source integration bootstrap. Existing application targets are reused intact;
# an independent consumer needs no parent-provided repository variables.
if(NOT WIN32 AND NOT ANDROID)
    message(FATAL_ERROR "Pixels SDK supports Windows and Android builds; Apple adapters are not implemented")
endif()
if(TARGET px_message)
    # Existing applications currently discover FFmpeg/Opus at their composition root.
    return()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
set(PX_COMMON_HOST_COMPONENTS OFF)
set(TESTS_ENABLED OFF)
set(PX_PROJECT_BINARY_PATH "${CMAKE_CURRENT_BINARY_DIR}/dependencies")

add_compile_definitions(FMT_HEADER_ONLY ASIO2_ENABLE_SSL)
if(MSVC)
    add_compile_definitions(WIN32 UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SPDLOG_WCHAR_FILENAMES)
    add_compile_options(/bigobj /utf-8)
elseif(ANDROID)
    add_compile_definitions(FMT_CONSTEVAL=)
    add_compile_options(-femulated-tls -Wno-error=deprecated-declarations
        -Wno-error=missing-template-arg-list-after-template-kw -Wno-error=deprecated-literal-operator)
    set(ANDROID_OPENSSL_LIBS
        "${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/openssl/prebuilt/android/${ANDROID_ABI}/libssl.a"
        "${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/openssl/prebuilt/android/${ANDROID_ABI}/libcrypto.a")
endif()

find_package(Protobuf REQUIRED)
# Legacy shared implementations use relative project include forms. These are
# scoped to this SDK subtree; public target usage requirements are set below.
include_directories("${PX_PROJECT_PATH}" "${PX_PROJECT_PATH}/px_3rdparty")
add_subdirectory("${PX_PROJECT_PATH}/px_3rdparty" "${PX_PROJECT_BINARY_PATH}/px_3rdparty")
add_subdirectory("${PX_PROJECT_PATH}/px_common" "${PX_PROJECT_BINARY_PATH}/px_common")
add_subdirectory("${PX_PROJECT_PATH}/px_message" "${PX_PROJECT_BINARY_PATH}/px_message")
target_compile_definitions(px_common_core PUBLIC FMT_HEADER_ONLY ASIO2_ENABLE_SSL)
if(WIN32)
    target_compile_definitions(px_common_core PUBLIC WIN32 UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SPDLOG_WCHAR_FILENAMES)
    target_link_libraries(px_common_core PUBLIC winmm ws2_32 crypt32 shlwapi)
elseif(ANDROID)
    target_link_libraries(px_common_core PUBLIC log)
endif()

if(NOT PX_SDK_CORE_ONLY)
    find_package(Opus CONFIG REQUIRED)
    find_package(FFMPEG REQUIRED)
    add_subdirectory("${PX_PROJECT_PATH}/px_media_record" "${PX_PROJECT_BINARY_PATH}/px_media_record")
    add_subdirectory("${PX_PROJECT_PATH}/px_opus_codec" "${PX_PROJECT_BINARY_PATH}/px_opus_codec")
endif()
