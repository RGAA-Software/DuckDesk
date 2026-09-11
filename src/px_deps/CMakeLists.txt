add_subdirectory(px_3rdparty)

if(WIN32)
    set(SDL_SHARED OFF CACHE BOOL "Build the SDL3 shared library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build the SDL3 static library" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build the SDL3 test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Build SDL3 tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL3 examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Install SDL3" FORCE)
    add_subdirectory(px_sdl3 EXCLUDE_FROM_ALL SYSTEM)

    add_library(px_imgui STATIC
        px_imgui/imgui.cpp
        px_imgui/imgui_demo.cpp
        px_imgui/imgui_draw.cpp
        px_imgui/imgui_tables.cpp
        px_imgui/imgui_widgets.cpp
        px_imgui/backends/imgui_impl_dx11.cpp
        px_imgui/backends/imgui_impl_sdl3.cpp
        px_imgui/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(px_imgui SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/px_imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/px_imgui/backends"
    )
    target_link_libraries(px_imgui PUBLIC SDL3::SDL3-static d3d11.lib dxgi.lib)
endif()

#add_subdirectory(px_capture)
add_subdirectory(px_encoder)
add_subdirectory(px_message)
add_subdirectory(px_ft_engine)
add_subdirectory(px_media_record)
add_subdirectory(px_common)
add_subdirectory(px_rdp)
add_subdirectory(px_opus_codec)
add_subdirectory(px_voice_call)
add_subdirectory(px_steam_manager)
add_subdirectory(px_controller)
add_subdirectory(px_qt_widget)
add_subdirectory(px_relay_client)
add_subdirectory(px_server_protocol)
add_subdirectory(px_console_client)
add_subdirectory(px_webrtc_client)
add_subdirectory(px_account_sdk)
add_subdirectory(px_profile_client)
