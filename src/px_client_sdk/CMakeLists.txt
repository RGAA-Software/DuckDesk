if(ANDROID)
    find_package(FFMPEG REQUIRED)
elseif(NOT WIN32)
    message(FATAL_ERROR "The SDK platform adapter is currently implemented for Windows and Android only")
endif()

if(MSVC)
    add_compile_options("/bigobj")
endif()

add_subdirectory(connection)

set(SDK_SRCS
    thunder_sdk.cpp
    sdk_video_decoder.cpp
    sdk_ffmpeg_decoder.cpp
    sdk_ffmpeg_vulkan_decoder.cpp
    sdk_ffmpeg_soft_decoder.cpp
    sdk_mediacodec_video_decoder.cpp
    sdk_net_client.cpp
    sdk_stream_helper.cpp
    sdk_cast_receiver.cpp
    sdk_timer.cpp
    sdk_statistics.cpp
    sdk_errors.cpp
    video_decode_thread_task.cpp
    ${CONN_SRCS}
)

if(ANDROID)
    list(APPEND SDK_SRCS sdk_android_software_decoder.cpp)
endif()

# Frame data remains shared; actual presentation belongs to the platform client.
add_library(px_sdk ${SDK_SRCS} gl/raw_image.cpp)
set_target_properties(px_sdk PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
# Export both supported include forms: <sdk_header.h> and <px_client_sdk/sdk_header.h>.
# Consumers obtain the moved SDK root through the target, not the dependency tree.
target_include_directories(px_sdk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${PX_PROJECT_PATH}
        ${PX_PROJECT_PATH}/px_message
        ${PX_PROJECT_PATH}/px_3rdparty
        ${PX_PROJECT_PATH}/px_3rdparty/asio2/include
        ${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd
)
target_link_libraries(px_sdk PRIVATE Opus::opus)
target_link_libraries(px_sdk PRIVATE px_opus_codec px_message px_common)

message("ffmpeg libraries: ${FFMPEG_LIBRARIES}")

if (ANDROID)
    target_link_libraries(px_sdk PRIVATE ${FFMPEG_LIBRARIES})
    target_link_libraries(px_sdk PRIVATE ${ANDROID_OPENSSL_LIBS})
else ()
    target_link_libraries(px_sdk PRIVATE protobuf::libprotobuf)

    target_link_libraries(px_sdk PRIVATE
            ${FFMPEG_LIBRARIES}
    )
endif ()

if(BUILD_TESTING AND NOT ANDROID)
    # Qt is used by the file-transfer test harness only, never by px_sdk.
    find_package(Qt6 REQUIRED COMPONENTS Core)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_ft_transport_e2e
        tests/test_ft_transport_e2e.cpp
    )
    target_link_libraries(test_ft_transport_e2e PRIVATE
        px_sdk
        px_ft_engine
        px_common
        px_message
        Qt6::Core
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME test_ft_transport_e2e COMMAND test_ft_transport_e2e)
    add_executable(test_udp_media_state
        tests/test_udp_media_state.cpp
    )
    target_include_directories(test_udp_media_state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_udp_media_state PRIVATE
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME test_udp_media_state COMMAND test_udp_media_state)
    add_executable(test_udp_media_failure tests/test_udp_media_failure.cpp)
    target_link_libraries(test_udp_media_failure PRIVATE px_sdk px_message px_common GTest::gtest GTest::gtest_main)
    add_test(NAME udp_media_failure COMMAND test_udp_media_failure)
    add_executable(test_sdk_stream_helper
        tests/test_sdk_stream_helper.cpp
        sdk_stream_helper.cpp
    )
    target_include_directories(test_sdk_stream_helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_sdk_stream_helper PRIVATE
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME sdk_stream_helper COMMAND test_sdk_stream_helper)
    add_executable(test_sdk_websocket_reconnect
        tests/test_sdk_websocket_reconnect.cpp
    )
    target_link_libraries(test_sdk_websocket_reconnect PRIVATE
        px_sdk
        px_common
        GTest::gtest
        GTest::gtest_main
    )
    target_compile_definitions(test_sdk_websocket_reconnect PRIVATE
        PX_TEST_TLS_PEM="${CMAKE_SOURCE_DIR}/rust_server/px_console_server/media/default.pem")
    add_test(NAME sdk_websocket_reconnect COMMAND test_sdk_websocket_reconnect)
    set_tests_properties(sdk_websocket_reconnect PROPERTIES LABELS "render-lifecycle")
endif()
