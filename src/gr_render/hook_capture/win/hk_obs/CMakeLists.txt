cmake_minimum_required(VERSION 3.26)
#project(hk_obs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(injector)
#add_subdirectory(graphics)
add_subdirectory(ipc_util)
add_subdirectory(offsets)

cmake_minimum_required(VERSION 3.26)
project(tc_graphics)

set(CMAKE_CXX_STANDARD 23)
add_definitions(-DCOMPILE_VULKAN_HOOK)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)
#include_directories(${VCPKG_ROOT}/installed/x64-windows/include)
message("--> ${DETOURS_INCLUDE_DIRS}")
message("--> ${DETOURS_LIBRARY}")
message("--> ${VCPKG_ROOT}/packages/detours_x64-windows-static/include/detours")

#find_package(spdlog CONFIG REQUIRED)
#find_package(fmt CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED
        graphics/d3d8-capture.cpp
        graphics/d3d9-capture.cpp
        graphics/d3d10-capture.cpp
        graphics/d3d11-capture.cpp
        graphics/d3d12-capture.cpp
        graphics/dxgi-capture.cpp
        graphics/gl-capture.c
        graphics/graphics-hook.cpp
        graphics/graphics-hook.rc
        graphics/graphics-hook-config.h.in
        graphics/vulkan-capture.c
        obfuscate.c
        hook_manager.cpp
        app_shared_info_reader.cpp
        ws_ipc_client.cpp
        ../client_ipc_manager.cpp
        ../hk_video/d3d_utils.cpp
        ../hk_video/shared_texture.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC ${DETOURS_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PUBLIC ${DETOURS_LIBRARY} tc_ipc_util tc_common_new tc_message protobuf::libprotobuf dxgi d3d11 d3d12 dxguid Iphlpapi.lib)