cmake_minimum_required(VERSION 3.20)
project(px_protection LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(DETOURS_LIBRARY detours REQUIRED)

message("DETOURS_INCLUDE_DIRS--> ${DETOURS_INCLUDE_DIRS}")
message("DETOURS_LIBRARY--> ${DETOURS_LIBRARY}")

# Static library compiled into px_client.exe (no standalone DLL).
add_library(${PROJECT_NAME} STATIC
    px_protection.cpp
    px_protection.h
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${DETOURS_INCLUDE_DIRS}
)


target_link_libraries(${PROJECT_NAME} PRIVATE detours)
