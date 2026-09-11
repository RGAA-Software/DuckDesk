add_executable(px_panel_imgui_preview
    main.cpp
    network_settings_page.cpp
    panel_preview.cpp
)

target_link_libraries(px_panel_imgui_preview PRIVATE px_desktop_shell px_ui px_imgui SDL3::SDL3-static)

set_target_properties(px_panel_imgui_preview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
    WIN32_EXECUTABLE TRUE
)
target_link_options(px_panel_imgui_preview PRIVATE "/ENTRY:mainCRTStartup")

add_custom_command(TARGET px_panel_imgui_preview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../resources/qt_material/fonts/roboto/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts/Roboto-Regular.ttf"
)
