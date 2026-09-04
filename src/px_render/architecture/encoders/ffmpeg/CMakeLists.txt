project(enc_ffmpeg)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg_encoder_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg_encoder.cpp
)

add_library(${PROJECT_NAME} STATIC ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin)
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES} px_opus_codec_new yuv)# frame_render
