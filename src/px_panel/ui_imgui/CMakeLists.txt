add_library(px_panel_imgui_views STATIC
    network_settings_page.cpp
    network_settings_presenter.cpp
    panel_navigation.cpp
    panel_preview.cpp
    placeholder_page.cpp
    server_status_page.cpp
)

target_link_libraries(px_panel_imgui_views PUBLIC px_desktop_shell px_ui px_imgui SDL3::SDL3-static)

add_executable(px_panel_imgui_preview
    main.cpp
    preview_network_settings_port.cpp
    preview_server_status_port.cpp
)

target_link_libraries(px_panel_imgui_preview PRIVATE px_panel_imgui_views)

set_target_properties(px_panel_imgui_preview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
    WIN32_EXECUTABLE TRUE
)

add_executable(px_panel_imgui WIN32
    product_main.cpp
    panel_network_settings_port.cpp
    panel_server_status_port.cpp
    ${MAIN_SRCS}
    ${NETWORK_SRCS}
    ${CMAKE_BINARY_DIR}/src/px_base/icon.rc
    ${SERVICE_MGR_SRCS}
    ${DEVICE_LIST_SRCS}
    ${CMAKE_SOURCE_DIR}/src/px_client/network/ct_auth_token.cpp
)

set_target_properties(px_panel_imgui PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\" /SUBSYSTEM:WINDOWS")

target_link_libraries(px_panel_imgui PRIVATE
    px_panel_imgui_views
    Qt6::Widgets Qt6::Network Qt6::Core Qt6::Core5Compat Qt6::WebSockets Qt6::Charts Qt6::Svg
    unofficial::sqlite3::sqlite3 sqlite_orm::sqlite_orm px_common px_steam_manager px_vigem px_message px_cp_message px_qt_widget
    winmm.lib SetupAPI.lib gflags::gflags px_relay_client px_translator px_console_client Dwmapi.lib px_widget_loadings OpenSSL::Crypto
    px_account_sdk px_rp_message px_profile_client px_hw_info px_server_message skin_interface skin_loader gd_gui Advapi32.lib
    unofficial::breakpad::libbreakpad unofficial::breakpad::libbreakpad_client
)

set_target_properties(px_panel_imgui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}")

add_custom_command(TARGET px_panel_imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel_imgui>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../resources/qt_material/fonts/roboto/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_panel_imgui>/resources/fonts/Roboto-Regular.ttf"
)
target_link_options(px_panel_imgui_preview PRIVATE "/ENTRY:mainCRTStartup")

add_custom_command(TARGET px_panel_imgui_preview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../resources/qt_material/fonts/roboto/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts/Roboto-Regular.ttf"
)
