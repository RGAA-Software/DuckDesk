cmake_minimum_required(VERSION 3.26)
project(tc_controller)

set(CMAKE_CXX_STANDARD 20)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(vigem)

if (WIN32)

elseif (APPLE)

elseif (UNIX)

endif ()