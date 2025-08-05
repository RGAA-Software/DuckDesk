cmake_minimum_required(VERSION 3.26)
project(tc_graphics_lb)

set(CMAKE_CXX_STANDARD 20)

set(SRCS
    dll_main.cpp
    ../client_manager.cpp
)
add_library(${PROJECT_NAME} SHARED ${SRCS})
target_link_libraries(${PROJECT_NAME} tc_common_new EasyHook64)