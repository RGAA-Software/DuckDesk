cmake_minimum_required(VERSION 3.26)
project(px_capture_new)

set(CMAKE_CXX_STANDARD 23)

add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if(WIN32)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Core5Compat)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Core5Compat)
endif ()
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/win)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/win/hk_obs)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/win/hk_obs/d3d8_api)
include_directories(${VK_SDK_ROOT}/Include)

#find_package(libyuv CONFIG REQUIRED)

if (WIN32)

    ## capture
    set(CAPTURE_SRCS
        desktop_capture.h
        desktop_capture.cpp
        win/audio/audio_file_saver.cpp
        win/audio/wasapi_audio_capture.cpp
        win/desktop_capture/dda_capture.cpp
        win/desktop_capture/cursor_capture.cpp
    )
    add_library(px_capture_new ${CAPTURE_SRCS})
    target_link_libraries(px_capture_new tc_message protobuf::libprotobuf Winmm Qt6::Core Qt6::Core5Compat)

    ## capture dll
    set(DLL_SRCS
        win/client_manager.cpp
        win/dll_main.cpp

        ## hk_utils
        win/hk_utils/env.h
        win/hk_utils/module.hpp
        win/hk_utils/shellapi.h

        ## hk_video
        win/hk_video/capture_dxgi.cpp
        win/hk_video/capture_dxgi_d3d11.cpp
        win/hk_video/capture_dxgi_d3d11on12.cpp
        win/hk_video/capture_texture.cpp
        win/hk_video/d3d_utils.cpp
        win/hk_video/hook_dxgi.cpp
        win/hk_video/hook_event.cpp
        win/hk_video/shared_texture.cpp

        ## hk_audio
    )

    if (ENABLE_HOOK_CAPTURE)
        #add_library(tc_capture_library SHARED ${DLL_SRCS})
        #target_link_libraries(tc_capture_library px_common_new EasyHook64 Iphlpapi.lib)

    endif ()

    add_subdirectory(win/hk_obs)
    add_subdirectory(win/hk_gpu_lb)

elseif (APPLE)

elseif (UNIX)

endif ()

add_subdirectory(win/tests)