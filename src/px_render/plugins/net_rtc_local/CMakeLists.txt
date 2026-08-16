project(net_rtc_local)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DWEBRTC_WIN=1 -DWEBRTC_USE_H264=1)
add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
add_definitions(-DQT_DEPRECATED_WARNINGS -DQT_NO_KEYWORDS)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

message("RTC PLUGIN, WEBRTC_ROOT: ${WEBRTC_ROOT}")
include_directories(${WEBRTC_ROOT}/)
include_directories(${WEBRTC_ROOT}/include)
include_directories(${WEBRTC_ROOT}/include/third_party/abseil-cpp)
include_directories(${WEBRTC_ROOT}/include/third_party/jsoncpp/source/include/)
include_directories(${WEBRTC_ROOT}/include/base)
link_directories(${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/webrtc/x64)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_local_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_data_channel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/peer_callback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_video_encoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_video_encoder_factory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/h264_sei_helper.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/video_source_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_source_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/remote_audio_sink.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
# webrtc.lib ships its own BoringSSL symbols which conflict with vcpkg OpenSSL
# linked through px_common_new; /FORCE:MULTIPLE lets the linker pick one.
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
target_link_libraries(${PROJECT_NAME} PUBLIC px_common_new px_message px_net_plugin
        webrtc.lib
        Winmm
        ws2_32
        Secur32
        Crypt32
        Msdmo
        dmoguids
        wmcodecdspuuid Iphlpapi.lib
        dxgi d3d11 d3d12 dxguid Strmiids Quartz Ole32
        Shcore.lib Dwmapi.lib
)
