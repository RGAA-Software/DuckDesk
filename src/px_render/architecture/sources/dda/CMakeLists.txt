project(cap_dda)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODULE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/dda_capture_source.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/cursor_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dda_capture.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/desktop_capture.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/../desktop_capture_source.cpp
)

add_library(${PROJECT_NAME} STATIC ${MODULE_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin)
