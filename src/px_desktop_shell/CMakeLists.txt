add_library(px_desktop_shell STATIC
    d3d11_renderer.cpp
    desktop_shell.cpp
    font_loader.cpp
    imgui_session.cpp
    title_bar.cpp
    window_host.cpp
    windows_title_bar_behavior.cpp
)

target_include_directories(px_desktop_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(px_desktop_shell PUBLIC px_ui px_imgui SDL3::SDL3-static comctl32.lib d3d11.lib dxgi.lib)
