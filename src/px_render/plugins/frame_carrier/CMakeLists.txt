project(frame_carrier)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/frame_carrier_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/video_frame_carrier.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image_generator.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_steam_manager_new px_message px_plugin winmm.lib SetupAPI.lib yuv)
#target_link_libraries(${PROJECT_NAME} PRIVATE opencv_world)
