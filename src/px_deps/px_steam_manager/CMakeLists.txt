cmake_minimum_required(VERSION 3.26)
project(px_steam_manager)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if(MSVC)
    add_compile_options("/bigobj")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    set(SRCS
        steam_manager.cpp
    )
    add_library(${PROJECT_NAME} ${SRCS})
    target_link_libraries(${PROJECT_NAME} px_common)

elseif (APPLE)

elseif (UNIX)

endif ()
