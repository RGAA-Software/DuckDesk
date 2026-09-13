add_library(px_desktop_shell STATIC
    brand_logo.cpp
    d3d11_renderer.cpp
    d3d11_video_presenter.cpp
    desktop_renderer.cpp
    desktop_shell.cpp
    font_loader.cpp
    imgui_session.cpp
    platform_icon_atlas.cpp
    title_bar.cpp
    vulkan_renderer.cpp
    vulkan_libav_bridge.c
    window_host.cpp
    windows_title_bar_behavior.cpp
)

target_include_directories(px_desktop_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(Vulkan REQUIRED)
target_include_directories(px_desktop_shell PRIVATE "${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty")
target_link_libraries(px_desktop_shell PUBLIC px_ui px_imgui SDL3::SDL3-static px_common pixels::sdk Vulkan::Vulkan comctl32.lib dwmapi.lib d3d11.lib
    d3dcompiler.lib dxgi.lib "${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/libplacebo/lib/libplacebo.lib")

function(gammaray_stage_platform_icons target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/icons/platform"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/licenses"
    )
    foreach(platform_icon windows macos android ios)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/platform/${platform_icon}.png"
                "$<TARGET_FILE_DIR:${target}>/resources/icons/platform/${platform_icon}.png"
        )
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/platform/LICENSE.tabler"
            "$<TARGET_FILE_DIR:${target}>/resources/licenses/Tabler.txt"
    )
endfunction()
