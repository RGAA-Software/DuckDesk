project(cap_dda)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/dda_capture_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/cursor_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dda_capture.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/desktop_capture.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/../plugin_desktop_capture.cpp
)

add_library(${PROJECT_NAME} STATIC ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin)
