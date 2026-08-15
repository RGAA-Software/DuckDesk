cmake_minimum_required(VERSION 3.26)
project(px_encoder_new)

set(CMAKE_CXX_STANDARD 20)

find_package(FFMPEG REQUIRED)

include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})

set(NVSDK_VERSION
    12
)

include(nvencoder/${NVSDK_VERSION}/CMakeLists.txt)

include_directories(
    nvencoder/${NVSDK_VERSION}
)

set(NVENCODER_SOURCES_LIST)

foreach(item ${NVENCODER_SOURCES})
    #message("Current item: ${item}")
    list(APPEND NVENCODER_SOURCES_LIST "${item}")
endforeach()

set(SRCS
    ${NVENCODER_SOURCES_LIST}
    video_encoder.cpp
    video_encoder_factory.cpp
    ffmpeg_video_encoder.cpp
    nvenc_video_encoder.cpp
)

add_subdirectory(frame_render)
add_subdirectory(legacy)

add_library(px_encoder_new ${SRCS})
target_link_libraries(px_encoder_new ${FFMPEG_LIBRARIES} px_opus_codec_new frame_render yuv)

#add_subdirectory(tests)
