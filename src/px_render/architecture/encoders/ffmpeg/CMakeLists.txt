project(enc_ffmpeg)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

set(MODULE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg_video_encoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg_encoder.cpp
)

add_library(${PROJECT_NAME} STATIC ${MODULE_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message render_architecture_core)
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES} px_opus_codec_new yuv)# frame_render
