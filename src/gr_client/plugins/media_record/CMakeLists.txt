project(plugin_media_record_client)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/media_record_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/media_recorder.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE tc_common_new tc_message tc_client_plugin Qt6::Core Qt6::Widgets)

target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES} tc_opus_codec_new yuv)
target_include_directories(${PROJECT_NAME} PRIVATE
    
    ${CMAKE_SOURCE_DIR}/src/gr_client/plugin_interface
    ${CMAKE_SOURCE_DIR}/src/gr_client
)#${FFMPEG_LIBRARIES} frame_render
