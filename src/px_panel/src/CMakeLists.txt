add_subdirectory(render_panel)
add_subdirectory(render_panel/system)

set(IMGUI_MAIN_SRCS
        ${PANEL_IMGUI_SRCS}
        ${SYSTEM_SRCS}
        PARENT_SCOPE
)

add_subdirectory(skin)
