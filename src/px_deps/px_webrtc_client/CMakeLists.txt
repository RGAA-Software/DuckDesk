project(px_rtc_client)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DWEBRTC_WIN=1 -DWEBRTC_USE_H264=1)
add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
add_definitions(-DQT_DEPRECATED_WARNINGS -DQT_NO_KEYWORDS) #

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
#target_compile_options(${PROJECT_NAME}
#        PRIVATE
#        $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
#        $<$<CXX_COMPILER_ID:GNU>:-Wa,-mbig-obj>)

#set(WEBRTC_ROOT ${PX_PROJECT_PATH}/deps/px_3rdparty/webrtc)
include_directories(${WEBRTC_ROOT}/)
include_directories(${WEBRTC_ROOT}/include)
include_directories(${WEBRTC_ROOT}/include/third_party/abseil-cpp)
include_directories(${WEBRTC_ROOT}/include/third_party/jsoncpp/source/include/)
include_directories(${WEBRTC_ROOT}/include/base)
link_directories(${PX_PROJECT_PATH}/px_3rdparty/webrtc/x64)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(LIBRARY_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/peer_callback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_client.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_data_channel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_video_sink.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_encoded_frame_sink.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc_audio_sink.cpp
)

add_library(${PROJECT_NAME} SHARED ${LIBRARY_SRCS})
target_compile_definitions(${PROJECT_NAME} PRIVATE PX_RTC_CLIENT_BUILD=1)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME px_client_rtc)
#add_library(${PROJECT_NAME} ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new Qt6::Core Qt6::Widgets
        ${WEBRTC_ROOT}/x64/webrtc.lib
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

if(BUILD_TESTING)
    add_executable(test_rtc_client_dll_lifecycle
        tests/test_rtc_client_dll_lifecycle.cpp
    )
    target_link_libraries(test_rtc_client_dll_lifecycle PRIVATE
        px_rtc_client
        GTest::gtest
        GTest::gtest_main
    )
    add_custom_command(TARGET test_rtc_client_dll_lifecycle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_rtc_client_dll_lifecycle>
            $<TARGET_FILE_DIR:test_rtc_client_dll_lifecycle>
        COMMAND_EXPAND_LISTS
    )
    add_test(NAME rtc_client_dll_lifecycle COMMAND test_rtc_client_dll_lifecycle)
    set_tests_properties(rtc_client_dll_lifecycle PROPERTIES LABELS "render-lifecycle")
endif()
