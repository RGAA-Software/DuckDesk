cmake_minimum_required(VERSION 3.26)
project(GammaRayPremium LANGUAGES CXX C)

include(env_premium.cmake)
include(config_premium.cmake)

message("TARGET_TYPE: ===> ${TARGET_TYPE}")
if(TARGET_TYPE STREQUAL "OpenSource")
    message(STATUS "Building OpenSource version!!")
    # 添加 OpenSource 特定设置
    add_definitions(-DOPENSOURCE_BUILD)
elseif(TARGET_TYPE STREQUAL "Official")
    message(STATUS "Building Official version")
    add_definitions(-DOFFICIAL_BUILD)
else()
    set(TARGET_TYPE "Official")
    add_definitions(-DOFFICIAL_BUILD)
    message("TARGET_TYPE: ${TARGET_TYPE}, will use official type.")
endif()

set(CMAKE_PREFIX_PATH ${QT_ROOT}/lib/cmake)
set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

find_package(FFMPEG REQUIRED)
set(FFMPEG_ROOT ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET})

##Source
include_directories(${CMAKE_SOURCE_DIR}/)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/px_base)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/eventbus/src)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_message_new)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_server_protocol)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/qwindowkit/include)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/asio2/include)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/asio2/3rd)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/asio2/3rd/asio)
include_directories(${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include)
include_directories(${FFMPEG_ROOT}/include)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/webrtc/include/third_party/libyuv/include)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/qwindowkit)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/qwindowkit/src)
include_directories(${CMAKE_SOURCE_DIR}/src/px_panel/src)
include_directories(${CMAKE_SOURCE_DIR}/src/px_render)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_qt_widget)
include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_client_sdk_new)
##Binary
include_directories(${CMAKE_BINARY_DIR}/src/px_base)
include_directories(${CMAKE_BINARY_DIR}/src/px_panel)
include_directories(${CMAKE_BINARY_DIR}/src/px_deps/px_message_new)
include_directories(${CMAKE_BINARY_DIR}/src/px_deps/px_server_protocol)

## Client
# include_directories(${CMAKE_SOURCE_DIR}/src/px_client)
# include_directories(${CMAKE_SOURCE_DIR}/src/px_client/plugins)
# include_directories(${CMAKE_SOURCE_DIR}/src/px_client/plugins/file_transfer_client)
# include_directories(${CMAKE_SOURCE_DIR}/src/px_client/plugins/file_transfer_client/src)

# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins)
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client)
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client/src)

## Server
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRayServer)

link_directories(${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib)

if(MSVC)
    add_compile_options($<IF:$<CONFIG:Debug>,/MTd,/MT>)
    add_compile_options("/bigobj")
    include_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/openssl/include)
    link_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/openssl/x64/lib/VC)
endif()

add_definitions(-DFMT_HEADER_ONLY)

if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:83886080")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--stack,83886080")
endif()

if (MEMORY_STST_ENABLED)
    add_definitions(-DMEMORY_STST_ON=1)
endif ()

if (STAMP_LABEL_ON_DIRECT)
    add_definitions(-DSTAMP_LABEL_ON_DIRECT=1)
endif ()

add_compile_definitions(ASIO2_ENABLE_SSL) #ASIO2_ENABLE_LOG
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DENABLE_SSL=0 -D_AMD64_)#-DCMAKE_GENERATOR_PLATFORM=x64
add_definitions(-DSPDLOG_WCHAR_FILENAMES)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Charts Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Charts Svg)

find_package(SDL2 CONFIG REQUIRED)
set(GFLAGS_USE_TARGET_NAMESPACE ON)
find_package(gflags CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(unofficial-libvpx CONFIG REQUIRED)
find_package(FFTW3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
#find_package(Drogon CONFIG REQUIRED)
#find_package(bsoncxx CONFIG REQUIRED)
#find_package(mongocxx CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(unofficial-breakpad CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(libyuv CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(SqliteOrm CONFIG REQUIRED)
find_package(OpenCV REQUIRED)
#find_package(protobuf REQUIRED)
#find_package(gRPC REQUIRED)

set(WEBRTC_ROOT ${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/webrtc)

set(GR_PROJECT_PATH ${CMAKE_SOURCE_DIR}/src/px_deps)
if (BUILD_PREMIUM)
    set(GR_PROJECT_BINARY_PATH ${CMAKE_BINARY_DIR}/src/px_deps)
else()
    set(GR_PROJECT_BINARY_PATH ${CMAKE_BINARY_DIR})
endif()

# Use protobuf from vcpkg
set(protobuf_MODULE_COMPATIBLE ON)
find_package(protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf from vcpkg: ${protobuf_VERSION}")

find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "cargo not found. Rust toolchain is required to build px_sysinfo.")
endif()

set(GR_SYSINFO_EXE_NAME GammaRaySysInfo.exe)
set(GR_SERVICE_EXE_NAME GammaRayService.exe)
set(GR_SERVICE_MGR_EXE_NAME GammaRayServiceManager.exe)
set(GR_USER_PROXY_EXE_NAME GammaRayUserProxy.exe)
set(GR_CRASH_REPORTER_EXE_NAME GammaRayCrashReporter.exe)
set(GR_UNINSTALL_EXE_NAME GammaRayUninstall.exe)

set(GR_RUST_WORKSPACE_ROOT ${CMAKE_SOURCE_DIR}/rust_client)
set(GR_RUST_PROFILE debug)
set(GR_RUST_BUILD_FLAG "")
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(GR_RUST_PROFILE release)
    set(GR_RUST_BUILD_FLAG --release)
endif()
set(GR_SYSINFO_RUST_EXE ${GR_RUST_WORKSPACE_ROOT}/target/${GR_RUST_PROFILE}/${GR_SYSINFO_EXE_NAME})
set(GR_SERVICE_RUST_EXE ${GR_RUST_WORKSPACE_ROOT}/target/${GR_RUST_PROFILE}/${GR_SERVICE_EXE_NAME})
set(GR_SERVICE_MGR_RUST_EXE ${GR_RUST_WORKSPACE_ROOT}/target/${GR_RUST_PROFILE}/${GR_SERVICE_MGR_EXE_NAME})
set(GR_USER_PROXY_RUST_EXE ${GR_RUST_WORKSPACE_ROOT}/target/${GR_RUST_PROFILE}/${GR_USER_PROXY_EXE_NAME})
set(GR_CRASH_REPORTER_RUST_EXE ${GR_RUST_WORKSPACE_ROOT}/target/${GR_RUST_PROFILE}/${GR_CRASH_REPORTER_EXE_NAME})
set(GR_UNINSTALL_RUST_EXE ${GR_RUST_WORKSPACE_ROOT}/target/${GR_RUST_PROFILE}/${GR_UNINSTALL_EXE_NAME})

set(GR_SYSINFO_STAGED_EXE ${GR_PROJECT_BINARY_PATH}/${GR_SYSINFO_EXE_NAME})
set(GR_SERVICE_STAGED_EXE ${GR_PROJECT_BINARY_PATH}/${GR_SERVICE_EXE_NAME})
set(GR_SERVICE_MGR_STAGED_EXE ${GR_PROJECT_BINARY_PATH}/${GR_SERVICE_MGR_EXE_NAME})
set(GR_USER_PROXY_STAGED_EXE ${GR_PROJECT_BINARY_PATH}/${GR_USER_PROXY_EXE_NAME})
set(GR_CRASH_REPORTER_STAGED_EXE ${GR_PROJECT_BINARY_PATH}/${GR_CRASH_REPORTER_EXE_NAME})
set(GR_UNINSTALL_STAGED_EXE ${GR_PROJECT_BINARY_PATH}/${GR_UNINSTALL_EXE_NAME})

add_custom_target(GammaRaySysInfo_rust
        COMMAND ${CARGO_EXECUTABLE} build --locked --manifest-path ${GR_RUST_WORKSPACE_ROOT}/Cargo.toml -p px_sysinfo --bin GammaRaySysInfo ${GR_RUST_BUILD_FLAG}
        WORKING_DIRECTORY ${GR_RUST_WORKSPACE_ROOT}
        COMMENT "Building Rust GammaRaySysInfo"
        VERBATIM
)

add_custom_target(GammaRayService_rust
        COMMAND ${CARGO_EXECUTABLE} build --locked --manifest-path ${GR_RUST_WORKSPACE_ROOT}/Cargo.toml -p px_service --bin GammaRayService ${GR_RUST_BUILD_FLAG}
        WORKING_DIRECTORY ${GR_RUST_WORKSPACE_ROOT}
        COMMENT "Building Rust GammaRayService"
        VERBATIM
)

add_custom_target(GammaRayServiceManager_rust
        COMMAND ${CARGO_EXECUTABLE} build --locked --manifest-path ${GR_RUST_WORKSPACE_ROOT}/Cargo.toml -p service_manager --bin GammaRayServiceManager ${GR_RUST_BUILD_FLAG}
        WORKING_DIRECTORY ${GR_RUST_WORKSPACE_ROOT}
        COMMENT "Building Rust GammaRayServiceManager"
        VERBATIM
)

add_custom_target(GammaRayUserProxy_rust
        COMMAND ${CARGO_EXECUTABLE} build --locked --manifest-path ${GR_RUST_WORKSPACE_ROOT}/Cargo.toml -p px_user_proxy --bin GammaRayUserProxy ${GR_RUST_BUILD_FLAG}
        WORKING_DIRECTORY ${GR_RUST_WORKSPACE_ROOT}
        COMMENT "Building Rust GammaRayUserProxy"
        VERBATIM
)

add_custom_target(GammaRayCrashReporter_rust
        COMMAND ${CARGO_EXECUTABLE} build --locked --manifest-path ${GR_RUST_WORKSPACE_ROOT}/Cargo.toml -p px_crash_reporter --bin GammaRayCrashReporter ${GR_RUST_BUILD_FLAG}
        WORKING_DIRECTORY ${GR_RUST_WORKSPACE_ROOT}
        COMMENT "Building Rust GammaRayCrashReporter"
        VERBATIM
)

add_custom_target(GammaRaySysInfo_stage
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GR_SYSINFO_RUST_EXE}" "${GR_SYSINFO_STAGED_EXE}"
        DEPENDS GammaRaySysInfo_rust
        COMMENT "Staging Rust GammaRaySysInfo"
        VERBATIM
)

add_custom_target(GammaRayService_stage
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GR_SERVICE_RUST_EXE}" "${GR_SERVICE_STAGED_EXE}"
        DEPENDS GammaRayService_rust
        COMMENT "Staging Rust GammaRayService"
        VERBATIM
)

add_custom_target(GammaRayServiceManager_stage
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GR_SERVICE_MGR_RUST_EXE}" "${GR_SERVICE_MGR_STAGED_EXE}"
        DEPENDS GammaRayServiceManager_rust
        COMMENT "Staging Rust GammaRayServiceManager"
        VERBATIM
)

add_custom_target(GammaRayUserProxy_stage
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GR_USER_PROXY_RUST_EXE}" "${GR_USER_PROXY_STAGED_EXE}"
        DEPENDS GammaRayUserProxy_rust
        COMMENT "Staging Rust GammaRayUserProxy"
        VERBATIM
)

add_custom_target(GammaRayCrashReporter_stage
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GR_CRASH_REPORTER_RUST_EXE}" "${GR_CRASH_REPORTER_STAGED_EXE}"
        DEPENDS GammaRayCrashReporter_rust
        COMMENT "Staging Rust GammaRayCrashReporter"
        VERBATIM
)

add_custom_target(GammaRayUninstall_rust
        COMMAND ${CARGO_EXECUTABLE} build --locked --manifest-path ${GR_RUST_WORKSPACE_ROOT}/Cargo.toml -p px_uninstall --bin GammaRayUninstall ${GR_RUST_BUILD_FLAG}
        WORKING_DIRECTORY ${GR_RUST_WORKSPACE_ROOT}
        COMMENT "Building Rust GammaRayUninstall"
        VERBATIM
)

add_custom_target(GammaRayUninstall_stage
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GR_UNINSTALL_RUST_EXE}" "${GR_UNINSTALL_STAGED_EXE}"
        DEPENDS GammaRayUninstall_rust
        COMMENT "Staging Rust GammaRayUninstall"
        VERBATIM
)

add_subdirectory(src)

add_custom_target(px_build_premium_all ALL COMMENT "Build all targets")
add_dependencies(px_build_premium_all
    ### main executables
    GammaRay

    ### render
    plugin_amf_encoder plugin_file_transfer plugin_frame_carrier plugin_frame_resizer plugin_gdi_capture
    plugin_joystick plugin_media_recorder plugin_net_relay plugin_net_rtc plugin_nvenc_encoder
    plugin_obj_detector plugin_clipboard px_graphics px_graphics_util px_graphics_offsets
    plugin_net_rtc_local

    ### client
    plugin_media_record_client plugin_client_clipboard plugin_file_transfer_client

    ### skins
    skin_interface skin_official skin_loader skin_opensource

    ### anti hooking
    px_protection

    ### from former px_build_all (deps + rust stages)
    GammaRayRender GammaRayService_stage GammaRayServiceManager_stage px_message px_vigem px_plugin
    GammaRayClientInner px_relay_client px_rtc_client px_cp_message GammaRayUserProxy_stage px_hw_info
    GammaRayCrashReporter_stage GammaRaySysInfo_stage
    plugin_net_ws plugin_ffmpeg_encoder plugin_frame_debugger plugin_dda_capture
    plugin_was_audio_capture plugin_opus_encoder px_sdk px_qt_widget plugin_mock_video_stream px_translator
    px_cms_client px_account_sdk px_rp_message px_profile_client plugin_event_replayer px_global_id_generator
    px_server_message
)

if(PLUGIN_NET_UDP_ENABLED)
    add_dependencies(px_build_premium_all plugin_net_udp)
endif()

## Servers
#add_custom_target(px_build_servers_all ALL COMMENT "Build all targets")
#add_dependencies(px_build_servers_all px_svr_base px_relay_server px_cms_server px_auth_server px_profile_server)

set(RENDER_BINARY_ROOT ${CMAKE_BINARY_DIR}/src/px_deps)

## Copy proto files
set(WEBCLIENT_ROOT "${CMAKE_SOURCE_DIR}/src/px_web_client")
set(MESSAGE_PROTO_PATH ${CMAKE_SOURCE_DIR}/src/px_deps/px_message_new)
add_custom_command(TARGET px_build_premium_all PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_signaling_message.proto"  "${RENDER_BINARY_ROOT}/web/proto/px_signaling_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_file_transfer.proto"  "${RENDER_BINARY_ROOT}/web/proto/px_file_transfer.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_message.proto"  "${RENDER_BINARY_ROOT}/web/proto/px_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_client_panel_message.proto"  "${RENDER_BINARY_ROOT}/web/proto/px_client_panel_message.proto"

    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_signaling_message.proto"  "${WEBCLIENT_ROOT}/proto/px_signaling_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_file_transfer.proto"  "${WEBCLIENT_ROOT}/proto/px_file_transfer.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_message.proto"  "${WEBCLIENT_ROOT}/proto/px_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/px_client_panel_message.proto"  "${WEBCLIENT_ROOT}/proto/px_client_panel_message.proto"
)

add_custom_command(TARGET px_build_premium_all POST_BUILD
    ### Copy setup tools (NSIS script, images, Python scripts, etc.) into build dir
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/setup" "${CMAKE_BINARY_DIR}/setup"

    ### Collect all artifacts to dist/
    COMMAND python "${CMAKE_SOURCE_DIR}/scripts/collect_dist.py" --build-dir "${CMAKE_BINARY_DIR}"
)