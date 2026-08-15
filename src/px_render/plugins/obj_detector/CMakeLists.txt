project(plugin_obj_detector)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/obj_detector_plugin.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_steam_manager_new tc_message tc_plugin winmm.lib SetupAPI.lib)

