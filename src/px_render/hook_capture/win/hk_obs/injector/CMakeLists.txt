cmake_minimum_required(VERSION 3.26)
project(px_graphics_util)

add_executable(${PROJECT_NAME}
        inject-helper.c
        inject-library.c
        ../obfuscate.c
)