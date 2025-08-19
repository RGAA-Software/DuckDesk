cmake_minimum_required(VERSION 3.26)
include(env_premium.cmake)

set(CMAKE_PREFIX_PATH ${QT_ROOT}/lib/cmake)
set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

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
#include_directories(${VCPKG_ROOT}/installed/x64-windows/include)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/sqlite35)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/sqlite_orm/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/ffmpeg/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/webrtc/include/third_party/libyuv/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/leveldb/include)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/qwindowkit)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/qwindowkit/src)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/src)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/src/render)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_qt_widget)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_client_sdk_new)
##Binary
include_directories(${CMAKE_BINARY_DIR}/src/GammaRay)
include_directories(${CMAKE_BINARY_DIR}/src/GammaRay/deps/tc_message_new)
include_directories(${CMAKE_BINARY_DIR}/src/GammaRay/deps/tc_server_protocol)

## Client
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/src/client)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/src/client/plugins)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/src/client/plugins/file_transfer_client)
include_directories(${CMAKE_SOURCE_DIR}/src/GammaRay/src/client/plugins/file_transfer_client/src)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client/src)

link_directories(${VCPKG_ROOT}/installed/x64-windows/lib)

if(MSVC)
    add_compile_options($<IF:$<CONFIG:Debug>,/MTd,/MT>)
    add_compile_options("/bigobj")
    #include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/ffmpeg/include)
    #link_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/ffmpeg/lib/x64)

    #include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/openssl/include)
    #link_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/openssl/x64/lib/VC)
endif()

if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:83886080")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--stack,83886080")
endif()

add_compile_definitions(ASIO2_ENABLE_SSL) #ASIO2_ENABLE_LOG
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DENABLE_SSL=0 -D_AMD64_)#-DCMAKE_GENERATOR_PLATFORM=x64
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
find_package(Drogon CONFIG REQUIRED)
find_package(bsoncxx CONFIG REQUIRED)
find_package(mongocxx CONFIG REQUIRED)
#find_package(protobuf REQUIRED)
#find_package(gRPC REQUIRED)

set(WEBRTC_ROOT ${CMAKE_SOURCE_DIR}/src/GammaRay/deps/tc_3rdparty/webrtc)

add_subdirectory(src)

add_custom_target(tc_build_premium_all ALL COMMENT "Build all targets")
add_dependencies(tc_build_premium_all tc_build_all
        ### render
        plugin_amf_encoder plugin_file_transfer plugin_frame_carrier plugin_frame_resizer plugin_gdi_capture
        plugin_joystick plugin_media_recorder plugin_net_relay plugin_net_rtc plugin_net_udp plugin_nvenc_encoder
        plugin_obj_detector plugin_clipboard tc_graphics tc_graphics_util tc_graphics_offsets panel_companion
        plugin_net_rtc_local plugin_ssl_proxy

        ### client
        plugin_media_record_client plugin_client_clipboard plugin_file_transfer_client
)

## Servers
add_custom_target(tc_build_servers_all ALL COMMENT "Build all targets")
add_dependencies(tc_build_servers_all gr_relay_server gr_spvr_server gr_auth_server gr_profile_server)

set(RENDER_BINARY_ROOT ${CMAKE_BINARY_DIR}/src/GammaRay)
set(RENDER_PLUGIN_SOURCE_ROOT ${CMAKE_SOURCE_DIR}/src/render_plugins)
set(RENDER_PLUGIN_BINARY_ROOT ${CMAKE_BINARY_DIR}/src/render_plugins)
set(RENDER_PLUGIN_BINARY_TARGET_ROOT ${CMAKE_BINARY_DIR}/src/GammaRay/gr_plugins)

set(CLIENT_PLUGIN_SOURCE_ROOT ${CMAKE_SOURCE_DIR}/src/client_plugins)
set(CLIENT_PLUGIN_BINARY_ROOT ${CMAKE_BINARY_DIR}/src/client_plugins)
set(CLIENT_PLUGIN_BINARY_TARGET_ROOT ${CMAKE_BINARY_DIR}/src/GammaRay/gr_plugins_client)

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
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/file_transfer/plugin_file_transfer.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_file_transfer.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/file_transfer/plugin_file_transfer.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_file_transfer.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/net_relay/plugin_net_relay.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_relay.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/net_relay/plugin_net_relay.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_relay.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/clipboard/plugin_clipboard.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_clipboard.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/clipboard/plugin_clipboard.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_clipboard.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/gdi_capture/plugin_gdi_capture.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_gdi_capture.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/gdi_capture/plugin_gdi_capture.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_gdi_capture.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/frame_carrier/plugin_frame_carrier.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_frame_carrier.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/frame_carrier/plugin_frame_carrier.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_frame_carrier.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/frame_carrier/plugin_frame_carrier.dll"  "${CMAKE_SOURCE_DIR}/src/GammaRay/src/render/plugins/frame_carrier/plugin_frame_carrier.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/frame_carrier/plugin_frame_carrier.dll.toml"  "${CMAKE_SOURCE_DIR}/src/GammaRay/src/render/plugins/frame_carrier/plugin_frame_carrier.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/joystick/plugin_joystick.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_joystick.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/joystick/plugin_joystick.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_joystick.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/net_udp/plugin_net_udp.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_udp.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/net_udp/plugin_net_udp.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_udp.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/net_rtc/plugin_net_rtc.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_rtc.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/net_rtc/plugin_net_rtc.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_rtc.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/media_recorder/plugin_media_recorder.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_media_recorder.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/media_recorder/plugin_media_recorder.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_media_recorder.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/obj_detector/plugin_obj_detector.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_obj_detector.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/obj_detector/plugin_obj_detector.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_obj_detector.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/amf_encoder/plugin_amf_encoder.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_amf_encoder.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/amf_encoder/plugin_amf_encoder.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_amf_encoder.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/nvenc_encoder/plugin_nvenc_encoder.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_nvenc_encoder.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/nvenc_encoder/plugin_nvenc_encoder.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_nvenc_encoder.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/frame_resizer/plugin_frame_resizer.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_frame_resizer.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/frame_resizer/plugin_frame_resizer.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_frame_resizer.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/net_rtc_local/plugin_net_rtc_local.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_rtc_local.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/net_rtc_local/plugin_net_rtc_local.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_net_rtc_local.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_BINARY_ROOT}/ssl_proxy/plugin_ssl_proxy.dll"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_ssl_proxy.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${RENDER_PLUGIN_SOURCE_ROOT}/ssl_proxy/plugin_ssl_proxy.dll.toml"  "${RENDER_PLUGIN_BINARY_TARGET_ROOT}/plugin_ssl_proxy.dll.toml"

        #### Clients ####
        COMMAND ${CMAKE_COMMAND} -E copy "${CLIENT_PLUGIN_BINARY_ROOT}/media_record/plugin_media_record_client.dll"  "${CLIENT_PLUGIN_BINARY_TARGET_ROOT}/plugin_media_record_client.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLIENT_PLUGIN_SOURCE_ROOT}/media_record/plugin_media_record_client.dll.toml"  "${CLIENT_PLUGIN_BINARY_TARGET_ROOT}/plugin_media_record_client.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${CLIENT_PLUGIN_BINARY_ROOT}/clipboard/plugin_client_clipboard.dll"  "${CLIENT_PLUGIN_BINARY_TARGET_ROOT}/plugin_client_clipboard.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLIENT_PLUGIN_SOURCE_ROOT}/clipboard/plugin_client_clipboard.dll.toml"  "${CLIENT_PLUGIN_BINARY_TARGET_ROOT}/plugin_client_clipboard.dll.toml"

        COMMAND ${CMAKE_COMMAND} -E copy "${CLIENT_PLUGIN_BINARY_ROOT}/file_transfer_client/plugin_file_transfer_client.dll"  "${CLIENT_PLUGIN_BINARY_TARGET_ROOT}/plugin_file_transfer_client.dll"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLIENT_PLUGIN_SOURCE_ROOT}/file_transfer_client/plugin_file_transfer_client.dll.toml"  "${CLIENT_PLUGIN_BINARY_TARGET_ROOT}//plugin_file_transfer_client.dll.toml"

        ### Hook Capture ###
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/src/hook_capture/win/hk_obs/tc_graphics.dll" ${RENDER_BINARY_ROOT}
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/src/hook_capture/win/hk_obs/injector/tc_graphics_util.exe" ${RENDER_BINARY_ROOT}
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/src/hook_capture/win/hk_obs/offsets/tc_graphics_offsets.exe" ${RENDER_BINARY_ROOT}

        ### Panel Companion ###
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/src/panel_companion/panel_companion.dll" ${RENDER_BINARY_ROOT}
)