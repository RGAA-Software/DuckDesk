project(cap_gdi)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/gdi_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gdi_capture_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../desktop_capture_source.cpp
)

add_library(${PROJECT_NAME} STATIC ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_steam_manager_new px_message px_plugin winmm.lib SetupAPI.lib)
