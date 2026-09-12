add_library(px_panel_imgui_views STATIC
    account_control.cpp
    about_settings_page.cpp
    cloud_applications_page.cpp
    controller_settings_page.cpp
    general_settings_page.cpp
    network_settings_page.cpp
    network_settings_presenter.cpp
    notification_center.cpp
    panel_navigation.cpp
    panel_preview.cpp
    remote_control_page.cpp
    server_status_page.cpp
    settings_page.cpp
    security_settings_page.cpp
    security_records_page.cpp
)

target_link_libraries(px_panel_imgui_views PUBLIC px_desktop_shell px_ui px_imgui SDL3::SDL3-static)

add_executable(px_panel_imgui_preview
    main.cpp
    preview_account_port.cpp
    preview_cloud_applications_port.cpp
    preview_network_settings_port.cpp
    preview_remote_control_port.cpp
    preview_server_status_port.cpp
    preview_settings_port.cpp
    preview_security_records_port.cpp
)

target_link_libraries(px_panel_imgui_preview PRIVATE px_panel_imgui_views)

set_target_properties(px_panel_imgui_preview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
    WIN32_EXECUTABLE TRUE
)

add_executable(px_panel WIN32
    product_main.cpp
    product/panel_client_launcher.cpp
    product/panel_connection_input.cpp
    product/panel_connection_links.cpp
    product/panel_running_pipe.cpp
    product/render_api.cpp
    product/panel_credential_vault.cpp
    product/panel_audit_store.cpp
    product/panel_config_store.cpp
    product/panel_console_session.cpp
    product/panel_local_server.cpp
    product/panel_product_runtime.cpp
    product/panel_service_bridge.cpp
    product/panel_worker.cpp
    product/product_account_port.cpp
    product/product_cloud_applications_port.cpp
    product/product_network_settings_port.cpp
    product/product_remote_control_port.cpp
    product/product_security_records_port.cpp
    product/product_server_status_port.cpp
    product/product_settings_port.cpp
    product/product_voice_call_consent_overlay.cpp
    ${CMAKE_BINARY_DIR}/src/px_base/icon.rc
)

target_include_directories(px_panel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/product)
set_target_properties(px_panel PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
target_link_options(px_panel PRIVATE "/ENTRY:mainCRTStartup")

target_link_libraries(px_panel PRIVATE
    px_panel_imgui_views
    px_common
    px_console_client
    px_cp_message
    px_rp_message
    px_message
    unofficial::sqlite3::sqlite3
    OpenSSL::Crypto
    Advapi32.lib
)

set_target_properties(px_panel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}")

add_custom_command(TARGET px_panel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_panel>/resources/fonts/Roboto-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/libplacebo/lib/libplacebo-349.dll"
        "$<TARGET_FILE_DIR:px_panel>/libplacebo-349.dll"
)
target_link_options(px_panel_imgui_preview PRIVATE "/ENTRY:mainCRTStartup")

add_custom_command(TARGET px_panel_imgui_preview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts/Roboto-Regular.ttf"
)

if(BUILD_TESTING AND TARGET GTest::gtest)
    add_executable(px_panel_product_tests
        product/tests/panel_product_tests.cpp
        product/panel_audit_store.cpp
        product/panel_connection_input.cpp
        product/panel_connection_links.cpp
        product/panel_config_store.cpp
        product/panel_worker.cpp
    )
    target_include_directories(px_panel_product_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/product)
    target_link_libraries(px_panel_product_tests PRIVATE
        px_common
        px_ui
        px_rp_message
        unofficial::sqlite3::sqlite3
        OpenSSL::Crypto
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME px_panel_product_tests COMMAND px_panel_product_tests)
endif()
