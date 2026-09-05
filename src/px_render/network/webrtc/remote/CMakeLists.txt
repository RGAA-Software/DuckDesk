project(net_rtc)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DWEBRTC_WIN=1 -DWEBRTC_USE_H264=1)
add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
add_definitions(-DQT_DEPRECATED_WARNINGS -DQT_NO_KEYWORDS)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
#target_compile_options(${PROJECT_NAME}
#        PRIVATE
#        $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
#        $<$<CXX_COMPILER_ID:GNU>:-Wa,-mbig-obj>)

message("WebRTC remote network library, WEBRTC_ROOT: ${WEBRTC_ROOT}")

set(LIBRARY_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/../webrtc_execution_context.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_remote_transport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/i420_creator.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/video_source_mock.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/peer_callback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/desktop_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/desktop_capture_source.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_data_channel.cpp
)

add_library(${PROJECT_NAME} SHARED ${LIBRARY_SRCS})
target_compile_definitions(${PROJECT_NAME} PRIVATE PX_NET_RTC_BUILD=1)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${WEBRTC_ROOT}/
        ${WEBRTC_ROOT}/include
        ${WEBRTC_ROOT}/include/third_party/abseil-cpp
        ${WEBRTC_ROOT}/include/third_party/jsoncpp/source/include/
        ${WEBRTC_ROOT}/include/base
)
target_link_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/webrtc/x64
)
#add_library(${PROJECT_NAME} ${LIBRARY_SRCS})
# webrtc.lib ships its own BoringSSL symbols which conflict with vcpkg OpenSSL
# linked through px_common; /FORCE:MULTIPLE lets the linker pick one.
set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/FORCE:MULTIPLE"
        OUTPUT_NAME px_render_rtc_remote
)
target_link_libraries(${PROJECT_NAME} PRIVATE px_common px_message
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
