cmake_minimum_required(VERSION 3.20)
project(tc_protection LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(DETOURS_LIBRARY detours REQUIRED)

message("DETOURS_INCLUDE_DIRS--> ${DETOURS_INCLUDE_DIRS}")
message("DETOURS_LIBRARY--> ${DETOURS_LIBRARY}")

add_library(${PROJECT_NAME} SHARED
    tc_protection.cpp
    tc_protection.h
)

# 定义宏，确保导出符号
target_compile_definitions(${PROJECT_NAME} PRIVATE TC_PROTECTION_LIBRARY)

target_include_directories(${PROJECT_NAME}
    PUBLIC              
        ${CMAKE_CURRENT_SOURCE_DIR}     # antihookingprotection.h 所在目录
    PRIVATE
        ${DETOURS_INCLUDE_DIRS}
)


target_link_libraries(${PROJECT_NAME} PRIVATE detours)

