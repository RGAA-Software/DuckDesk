project(joystick)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(vigem)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/joystick_plugin.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_steam_manager_new px_message px_plugin px_js_vigem)
