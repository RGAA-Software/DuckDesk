cmake_minimum_required(VERSION 3.26)
project(px_gh_injector)

add_executable(${PROJECT_NAME}
        inject-helper.c
        inject-library.c
        ../obfuscate.c
)