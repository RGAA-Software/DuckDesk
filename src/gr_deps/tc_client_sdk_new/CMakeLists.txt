#find_package(websocketpp REQUIRED)
#find_package(Opus CONFIG REQUIRED)
#find_package(libyuv CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
#find_package(Protobuf CONFIG REQUIRED)

if(ANDROID)
    find_package(FFMPEG REQUIRED)
else()
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Core OpenGL OpenGLWidgets)
endif()

if(MSVC)
    add_compile_options("/bigobj")
endif()

include_directories(${GR_PROJECT_PATH}/)
include_directories(${GR_PROJECT_PATH}/tc_message_new)
include_directories(${GR_PROJECT_PATH}/tc_3rdparty/)
include_directories(${GR_PROJECT_PATH}/tc_3rdparty/eventbus/src)
include_directories(${GR_PROJECT_PATH}/tc_3rdparty/asio2/include)
include_directories(${GR_PROJECT_PATH}/tc_3rdparty/asio2/3rd)

set(WEBRTC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../tc_webrtc_client/6167)
include_directories(${WEBRTC_ROOT}/)
include_directories(${WEBRTC_ROOT}/include)
include_directories(${WEBRTC_ROOT}/include/third_party/abseil-cpp)
include_directories(${WEBRTC_ROOT}/include/third_party/jsoncpp/source/include/)
include_directories(${WEBRTC_ROOT}/include/base)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

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

set(GL_SRCS
    gl/shader_program.cpp
    gl/director.cpp
    gl/renderer.cpp
    gl/sprite.cpp
    gl/raw_image.cpp
)

add_library(tc_sdk ${SDK_SRCS} ${GL_SRCS})
target_link_libraries(tc_sdk PRIVATE Opus::opus glm::glm)#websocketpp::websocketpp
target_link_libraries(tc_sdk PRIVATE tc_opus_codec_new tc_message)

message("ffmpeg libraries: ${FFMPEG_LIBRARIES}")

if (ANDROID)
    target_link_libraries(tc_sdk PRIVATE ${FFMPEG_LIBRARIES} tc_relay_client)
    target_link_libraries(tc_sdk PRIVATE ${ANDROID_OPENSSL_LIBS})
else ()
    target_link_libraries(tc_sdk PRIVATE protobuf::libprotobuf tc_relay_client)

    target_link_libraries(tc_sdk PRIVATE
            Qt6::Widgets Qt6::Core Qt6::OpenGL Qt6::OpenGLWidgets
            ${FFMPEG_LIBRARIES}
    )
endif ()