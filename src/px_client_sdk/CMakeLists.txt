cmake_minimum_required(VERSION 3.26)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PixelsNativeSdk LANGUAGES C CXX)
    include(CTest)
endif()
get_filename_component(PX_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../px_deps" ABSOLUTE)
get_filename_component(PX_SDK_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(PX_SDK_CORE_ONLY "Build transport without a decoder/session platform" OFF)
set(sdk_qt_test_default OFF)
if(TARGET Qt6::Core AND TARGET px_ft_engine)
    set(sdk_qt_test_default ON)
endif()
option(PX_SDK_BUILD_QT_TESTS "Build the optional Qt file-transfer UI test harness" ${sdk_qt_test_default})
include(cmake/sdk_dependencies.cmake)
include(cmake/sdk_source_sets.cmake)
add_subdirectory(media_transport)

if(NOT PX_SDK_CORE_ONLY AND ANDROID)
    set(PX_SDK_PLATFORM android)
elseif(NOT PX_SDK_CORE_ONLY AND WIN32)
    set(PX_SDK_PLATFORM windows)
else()
    set(PX_SDK_PLATFORM "${CMAKE_SYSTEM_NAME}")
endif()

# Object libraries keep one final SDK archive (including all its implementations),
# without introducing static-library cycles between the legacy session and decoders.
add_library(px_sdk_core OBJECT ${PX_SDK_CORE_SOURCES})
add_library(pixels::transport ALIAS px_sdk_core)
set(sdk_targets px_sdk_core)
if(NOT PX_SDK_CORE_ONLY)
    px_sdk_platform_sources(PX_SDK_PLATFORM_SOURCES "${PX_SDK_PLATFORM}")
    add_library(px_sdk_platform OBJECT ${PX_SDK_PLATFORM_SOURCES})
    add_library(px_sdk ${PX_SDK_SESSION_SOURCES})
    add_library(pixels::sdk ALIAS px_sdk)
    list(APPEND sdk_targets px_sdk_platform px_sdk)
endif()

foreach(sdk_target IN LISTS sdk_targets)
    target_compile_features(${sdk_target} PUBLIC cxx_std_23)
    set_target_properties(${sdk_target} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    if(MSVC)
        target_compile_options(${sdk_target} PRIVATE /bigobj)
    endif()
    target_include_directories(${sdk_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PX_PROJECT_PATH}
        ${PX_PROJECT_PATH}/px_message
        ${PX_PROJECT_PATH}/px_3rdparty
        ${PX_PROJECT_PATH}/px_3rdparty/asio2/include
        ${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd
    )
    target_link_libraries(${sdk_target} PRIVATE px_message)
endforeach()

# The transport core needs no desktop hardware helpers, renderer or codec.
target_link_libraries(px_sdk_core PUBLIC px_message px_common_async px_common_file px_common_crypto pixels::media_transport)
target_include_directories(px_sdk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PX_PROJECT_PATH}
    $<TARGET_PROPERTY:px_message,BINARY_DIR>
)

# Export both supported include forms: <sdk_header.h> and <px_client_sdk/sdk_header.h>.
# Consumers obtain the moved SDK root through the target, not the dependency tree.
if(NOT PX_SDK_CORE_ONLY)
    target_link_libraries(px_sdk_platform PRIVATE px_common_file)
    target_link_libraries(px_sdk PRIVATE px_common_file)
    target_include_directories(px_sdk PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_libraries(px_sdk PUBLIC px_sdk_core px_media_record px_voice_call_core
        PRIVATE px_sdk_platform Opus::opus px_opus_codec ${FFMPEG_LIBRARIES})
    target_link_libraries(px_sdk_platform PRIVATE ${FFMPEG_LIBRARIES})
    target_include_directories(px_sdk_platform PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()

if(ANDROID)
    # MediaCodec and native windows belong to the SDK adapter, not its JNI consumer.
    if(TARGET px_sdk_platform)
        target_link_libraries(px_sdk_platform PUBLIC android mediandk)
    endif()
    target_link_libraries(px_sdk_core PUBLIC ${ANDROID_OPENSSL_LIBS})
else()
    target_link_libraries(px_sdk_core PUBLIC protobuf::libprotobuf)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_test(NAME sdk_source_sets COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sdk_source_sets.cmake)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_sdk_voice_protocol tests/test_sdk_voice_protocol.cpp)
    set_target_properties(test_sdk_voice_protocol PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(test_sdk_voice_protocol PRIVATE pixels::transport GTest::gtest GTest::gtest_main)
    add_test(NAME sdk_voice_protocol COMMAND test_sdk_voice_protocol)
    add_executable(test_udp_voice_protocol tests/test_udp_voice_protocol.cpp)
    set_target_properties(test_udp_voice_protocol PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(test_udp_voice_protocol PRIVATE pixels::transport GTest::gtest GTest::gtest_main)
    add_test(NAME udp_voice_protocol COMMAND test_udp_voice_protocol)
    add_executable(test_sdk_clipboard_protocol tests/test_sdk_clipboard_protocol.cpp)
    set_target_properties(test_sdk_clipboard_protocol PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(test_sdk_clipboard_protocol PRIVATE pixels::transport GTest::gtest GTest::gtest_main)
    add_test(NAME sdk_clipboard_protocol COMMAND test_sdk_clipboard_protocol)
    if(NOT PX_SDK_CORE_ONLY)
        add_executable(test_sdk_voice_call tests/test_sdk_voice_call.cpp)
        set_target_properties(test_sdk_voice_call PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(test_sdk_voice_call PRIVATE pixels::sdk GTest::gtest GTest::gtest_main)
        add_test(NAME sdk_voice_call COMMAND test_sdk_voice_call)
        if(TARGET px_voice_call)
            add_executable(test_sdk_voice_audio_port tests/test_sdk_voice_audio_port.cpp)
            set_target_properties(test_sdk_voice_audio_port PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_link_libraries(test_sdk_voice_audio_port PRIVATE pixels::sdk px_voice_call GTest::gtest GTest::gtest_main)
            add_test(NAME sdk_voice_audio_port COMMAND test_sdk_voice_audio_port)
        endif()
        add_executable(test_sdk_recording_session tests/test_sdk_recording_session.cpp)
        target_link_libraries(test_sdk_recording_session PRIVATE px_sdk GTest::gtest GTest::gtest_main)
        add_test(NAME sdk_recording_session COMMAND test_sdk_recording_session)
        add_executable(test_sdk_decoder_factory tests/test_sdk_decoder_factory.cpp)
        target_link_libraries(test_sdk_decoder_factory PRIVATE px_sdk GTest::gtest GTest::gtest_main)
        add_test(NAME sdk_decoder_factory COMMAND test_sdk_decoder_factory)
        add_executable(test_av_frame_ownership tests/test_av_frame_ownership.cpp)
        target_link_libraries(test_av_frame_ownership PRIVATE px_sdk GTest::gtest GTest::gtest_main)
        add_test(NAME av_frame_ownership COMMAND test_av_frame_ownership)
    endif()
    add_executable(test_sdk_connection_params tests/test_sdk_connection_params.cpp)
    target_link_libraries(test_sdk_connection_params PRIVATE px_sdk_core GTest::gtest GTest::gtest_main)
    add_test(NAME sdk_connection_params COMMAND test_sdk_connection_params)
    if(PX_SDK_BUILD_QT_TESTS)
        # Opt-in only: SDK-only tests never require Qt.
        if(NOT TARGET px_ft_engine)
            message(FATAL_ERROR "PX_SDK_BUILD_QT_TESTS requires the host-provided px_ft_engine target")
        endif()
        find_package(Qt6 REQUIRED COMPONENTS Core)
        add_executable(test_ft_transport_e2e tests/test_ft_transport_e2e.cpp)
        target_link_libraries(test_ft_transport_e2e PRIVATE
            px_sdk_core px_ft_engine px_common px_message Qt6::Core GTest::gtest GTest::gtest_main)
        add_test(NAME test_ft_transport_e2e COMMAND test_ft_transport_e2e)
    endif()
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
    target_link_libraries(test_udp_media_failure PRIVATE px_sdk_core GTest::gtest GTest::gtest_main)
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
        px_sdk_core
        GTest::gtest
        GTest::gtest_main
    )
    target_compile_definitions(test_sdk_websocket_reconnect PRIVATE
        PX_TEST_TLS_PEM="${PX_SDK_REPOSITORY_ROOT}/rust_server/px_console_server/media/default.pem")
    add_test(NAME sdk_websocket_reconnect COMMAND test_sdk_websocket_reconnect)
    set_tests_properties(sdk_websocket_reconnect PROPERTIES LABELS "render-lifecycle")
endif()
