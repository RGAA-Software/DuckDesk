cmake_minimum_required(VERSION 3.26)
#project(hk_obs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(injector)
#add_subdirectory(graphics)
add_subdirectory(ipc_util)
add_subdirectory(offsets)

cmake_minimum_required(VERSION 3.26)
project(px_gh)

set(CMAKE_CXX_STANDARD 23)
add_definitions(-DCOMPILE_VULKAN_HOOK)
# UE5 等新游戏默认 D3D12 渲染:d3d12-capture(command queue hook)必须编进去,
# 否则 DXGI Present hook 之外没有 D3D12 纹理拷贝路径,UE5 默认配置永远采不到画面
add_definitions(-DCOMPILE_D3D12_HOOK)
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
        ../hk_video/d3d_utils.cpp
        ../hk_video/shared_texture.cpp
        ../hk_audio/HookCoreApi.cpp
        ../hk_audio/HookXAudio2.cpp
        ../hk_audio/HookDirectSound.cpp
        ../hk_audio/HookWaveOut.cpp
        ../hk_audio/AudioHookCommon.cpp
        ../hk_audio/AudioMixer.cpp
        ../hk_audio/AudioShare.cpp
        ../hk_audio/SimpleAudioFormatConverter.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
        ${DETOURS_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../hk_audio
)
target_link_libraries(${PROJECT_NAME} PUBLIC
        ${DETOURS_LIBRARY}
        px_ipc_util
        px_common_new
        px_capture_new
        px_message
        protobuf::libprotobuf
        dxgi d3d11 d3d12 dxguid
        Iphlpapi.lib
        ole32
        winmm
        dsound
)

# Local test: force in-process audio hook, inject into a game, expect WAV dump.
add_executable(test_hook_audio_wav test_hook_audio_wav.cpp)
target_link_libraries(test_hook_audio_wav PRIVATE px_common_new)
target_compile_definitions(test_hook_audio_wav PRIVATE UNICODE _UNICODE)
set_target_properties(test_hook_audio_wav PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
)

# Local test: process-loopback capture of game audio → WAV (Win10 2004+).
add_executable(test_game_audio_loopback test_game_audio_loopback.cpp)
target_link_libraries(test_game_audio_loopback PRIVATE ole32 mmdevapi)
target_compile_definitions(test_game_audio_loopback PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(test_game_audio_loopback PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
)

# Helper: external PID process-loopback → WAV (used by 内录 tooling / host-side tests).
add_executable(px_audio_pid_capture px_audio_pid_capture.cpp)
target_link_libraries(px_audio_pid_capture PRIVATE ole32 mmdevapi)
target_compile_definitions(px_audio_pid_capture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(px_audio_pid_capture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
)

# WASAPI sine player — reliable target for in-process hook audio test.
add_executable(test_wasapi_tone test_wasapi_tone.cpp)
target_link_libraries(test_wasapi_tone PRIVATE ole32 mmdevapi)
target_compile_definitions(test_wasapi_tone PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(test_wasapi_tone PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
)