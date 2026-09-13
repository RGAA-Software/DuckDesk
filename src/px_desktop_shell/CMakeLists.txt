add_library(px_desktop_shell STATIC
    atlas_image_loader.cpp
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

function(gammaray_stage_desktop_images target)
    set(platform_icon_names
        windows.png windows-150.png windows-200.png
        macos.png macos-150.png macos-200.png
        android.png android-150.png android-200.png
        ios.png ios-150.png ios-200.png
    )
    set(brand_icon_names px_icon.png px_icon-150.png px_icon-200.png)
    set(desktop_asset_sources
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/platform/LICENSE.tabler"
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/LICENSE.freetype"
    )
    foreach(platform_icon IN LISTS platform_icon_names)
        list(APPEND desktop_asset_sources "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/platform/${platform_icon}")
    endforeach()
    foreach(brand_icon IN LISTS brand_icon_names)
        list(APPEND desktop_asset_sources "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/brand/${brand_icon}")
    endforeach()
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${desktop_asset_sources}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/icons/platform"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/icons/brand"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/licenses"
    )
    foreach(platform_icon IN LISTS platform_icon_names)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/platform/${platform_icon}"
                "$<TARGET_FILE_DIR:${target}>/resources/icons/platform/${platform_icon}"
        )
    endforeach()
    foreach(brand_icon IN LISTS brand_icon_names)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/brand/${brand_icon}"
                "$<TARGET_FILE_DIR:${target}>/resources/icons/brand/${brand_icon}"
        )
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/platform/LICENSE.tabler"
            "$<TARGET_FILE_DIR:${target}>/resources/licenses/Tabler.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/LICENSE.freetype"
            "$<TARGET_FILE_DIR:${target}>/resources/licenses/FreeType.txt"
    )
endfunction()
