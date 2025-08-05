cmake_minimum_required(VERSION 3.26)
project(tc_graphics)

set(CMAKE_CXX_STANDARD 20)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

find_library(DETOURS_LIBRARY detours REQUIRED)
#include_directories(${VCPKG_ROOT}/installed/x64-windows/include)
message("--> ${DETOURS_INCLUDE_DIRS}")
message("--> ${DETOURS_LIBRARY}")
message("--> ${VCPKG_ROOT}/packages/detours_x64-windows-static/include/detours")

#find_package(spdlog CONFIG REQUIRED)
#find_package(fmt CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED
        d3d8-capture.cpp
        d3d9-capture.cpp
        d3d10-capture.cpp
        d3d11-capture.cpp
        d3d12-capture.cpp
        dxgi-capture.cpp
        gl-capture.c
        graphics-hook.cpp
        graphics-hook.rc
        graphics-hook-config.h.in
        vulkan-capture.c
        ../obfuscate.c
)
target_include_directories(${PROJECT_NAME} PUBLIC ${DETOURS_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PUBLIC ${DETOURS_LIBRARY} tc_ipc_util tc_common_new dxgi d3d11 d3d12 dxguid DirectXTex)