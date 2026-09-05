project(enc_nvenc)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

set(MODULE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/nvenc_encoder_module.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nvenc_video_encoder.cpp
)

add_library(${PROJECT_NAME} STATIC ${MODULE_SRCS} ${NVENCODER_SOURCES_LIST})
# NVENC enters the graphics driver while opening a D3D11 encode session.  Some
# driver/session combinations signal a structured exception rather than a C++
# NVENCException.  /EHa lets the guarded catch (...) paths return failure so
# EncoderThread can fall back instead of terminating px_render (and its game
# job) outright.
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /EHa)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE px_common px_steam_manager px_message
        render_architecture_core winmm.lib SetupAPI.lib)
