cmake_minimum_required(VERSION 3.26)
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

set(FFMPEG_ROOT ${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/ffmpeg7.1.1)

##Source
include_directories(${CMAKE_SOURCE_DIR}/)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/eventbus/src)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_message_new)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_server_protocol)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/qwindowkit/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/asio2/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/asio2/3rd)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/asio2/3rd/asio)
include_directories(${VCPKG_ROOT}/installed/x64-windows/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/sqlite35)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/sqlite_orm/include)
include_directories(${FFMPEG_ROOT}/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/webrtc/include/third_party/libyuv/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/leveldb/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/opencv_410/include)
set(OpenCV_Libs ${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/opencv_410/x64/vc16/lib/opencv_world4100.lib)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/qwindowkit)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/qwindowkit/src)
include_directories(${CMAKE_SOURCE_DIR}/src/gr_panel/src)
include_directories(${CMAKE_SOURCE_DIR}/src/gr_render)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_qt_widget)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_client_sdk_new)
##Binary
include_directories(${CMAKE_BINARY_DIR}/src/gr_panel)
include_directories(${CMAKE_BINARY_DIR}/src/GammaRay/deps/tc_message_new)
include_directories(${CMAKE_BINARY_DIR}/src/GammaRay/deps/tc_server_protocol)

## Client
# include_directories(${CMAKE_SOURCE_DIR}/src/gr_client)
# include_directories(${CMAKE_SOURCE_DIR}/src/gr_client/plugins)
# include_directories(${CMAKE_SOURCE_DIR}/src/gr_client/plugins/file_transfer_client)
# include_directories(${CMAKE_SOURCE_DIR}/src/gr_client/plugins/file_transfer_client/src)

# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins)
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client)
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client/src)

## Server
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRayServer)

link_directories(${VCPKG_ROOT}/installed/x64-windows/lib)

if(MSVC)
    add_compile_options($<IF:$<CONFIG:Debug>,/MTd,/MT>)
    add_compile_options("/bigobj")
    #include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/ffmpeg/include)
    #link_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/ffmpeg/lib/x64)

    include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/openssl/include)
    link_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/openssl/x64/lib/VC)
endif()

add_definitions(-DFMT_HEADER_ONLY)

if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:83886080")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--stack,83886080")
endif()

if (JEMALLOC_ENABLED)
    add_definitions(-DJEMALLOC_ON=1)
endif ()

if (MIMALLOC_ENABLED)
    add_definitions(-DMIMALLOC_ON=1)
endif ()

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
#find_package(protobuf REQUIRED)
#find_package(gRPC REQUIRED)
find_package(mimalloc CONFIG REQUIRED)

set(WEBRTC_ROOT ${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/webrtc)

add_subdirectory(src)

add_custom_target(tc_build_premium_all ALL COMMENT "Build all targets")
add_dependencies(tc_build_premium_all tc_build_all
    ### main executables
    GammaRay uninstall panel_shadow_deleter

    ### render
    plugin_amf_encoder plugin_file_transfer plugin_frame_carrier plugin_frame_resizer plugin_gdi_capture
    plugin_joystick plugin_media_recorder plugin_net_relay plugin_net_rtc plugin_net_udp plugin_nvenc_encoder
    plugin_obj_detector plugin_clipboard tc_graphics tc_graphics_util tc_graphics_offsets panel_companion
    plugin_net_rtc_local plugin_ssl_proxy

    ### client
    plugin_media_record_client plugin_client_clipboard plugin_file_transfer_client

    ### skins
    skin_interface skin_official

    ### anti hooking
    tc_protection
)

## Servers
#add_custom_target(tc_build_servers_all ALL COMMENT "Build all targets")
#add_dependencies(tc_build_servers_all gr_svr_base gr_relay_server gr_spvr_server gr_auth_server gr_profile_server)

set(RENDER_BINARY_ROOT ${CMAKE_BINARY_DIR}/src/GammaRay)

## Copy proto files
set(WEBCLIENT_ROOT "${CMAKE_SOURCE_DIR}/src/GammaRayWebClient")
set(MESSAGE_PROTO_PATH ${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_message_new)
add_custom_command(TARGET tc_build_premium_all PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_signaling_message.proto"  "${RENDER_BINARY_ROOT}/web/proto/tc_signaling_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_file_transfer.proto"  "${RENDER_BINARY_ROOT}/web/proto/tc_file_transfer.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_message.proto"  "${RENDER_BINARY_ROOT}/web/proto/tc_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_client_panel_message.proto"  "${RENDER_BINARY_ROOT}/web/proto/tc_client_panel_message.proto"

    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_signaling_message.proto"  "${WEBCLIENT_ROOT}/proto/tc_signaling_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_file_transfer.proto"  "${WEBCLIENT_ROOT}/proto/tc_file_transfer.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_message.proto"  "${WEBCLIENT_ROOT}/proto/tc_message.proto"
    COMMAND ${CMAKE_COMMAND} -E copy "${MESSAGE_PROTO_PATH}/tc_client_panel_message.proto"  "${WEBCLIENT_ROOT}/proto/tc_client_panel_message.proto"
)

add_custom_command(TARGET tc_build_premium_all POST_BUILD
    ### make setup
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/make_setup/make_setup_config.json"  "${RENDER_BINARY_ROOT}/package/make_setup_config.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/make_setup/make_setup.nsi"  "${RENDER_BINARY_ROOT}/package/make_setup.nsi"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/make_setup/make_setup.py"  "${RENDER_BINARY_ROOT}/package/make_setup.py"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/make_setup/start_make.bat"  "${RENDER_BINARY_ROOT}/package/start_make.bat"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/src/make_setup/save_pdb.py"  "${RENDER_BINARY_ROOT}/package/save_pdb.py"

    ### Collect all artifacts to dist/
    COMMAND python "${CMAKE_SOURCE_DIR}/scripts/collect_dist.py" --build-dir "${CMAKE_BINARY_DIR}"
)