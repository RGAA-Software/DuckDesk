cmake_minimum_required(VERSION 3.26)
project(tc_graphics_util)

add_executable(${PROJECT_NAME}
        inject-helper.c
        inject-library.c
        ../obfuscate.c
)