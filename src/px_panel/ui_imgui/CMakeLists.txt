add_library(px_panel_imgui_views STATIC
    account_avatar_picker.cpp
    account_control.cpp
    about_settings_page.cpp
    connection_progress_dialog.cpp
    connection_qr_dialog.cpp
    controller_settings_page.cpp
    device_list_page.cpp
    general_settings_page.cpp
    network_settings_page.cpp
    network_settings_presenter.cpp
    notification_center.cpp
    panel_navigation.cpp
    panel_preview.cpp
    remote_control_page.cpp
    remote_device_actions.cpp
    settings_page.cpp
    security_settings_page.cpp
    security_records_page.cpp
)

if(PX_CAPABILITY_CLOUD_APP_CATALOG)
    target_sources(px_panel_imgui_views PRIVATE cloud_applications_page.cpp)
endif()
if(PX_CAPABILITY_DESKTOP_HOST)
    target_sources(px_panel_imgui_views PRIVATE server_status_page.cpp)
endif()

target_link_libraries(px_panel_imgui_views PUBLIC px_panel_desktop_shell px_ui px_imgui SDL3::SDL3-static PRIVATE px_common Comdlg32.lib)
target_include_directories(px_panel_imgui_views BEFORE PRIVATE "${PX_PRODUCT_GENERATED_DIR}")

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
    product/connection_progress_tracker.cpp
    product/environment_diagnostics.cpp
    product/panel_connection_input.cpp
    product/panel_connection_links.cpp
    product/panel_device_name.cpp
    product/panel_running_pipe.cpp
    product/render_api.cpp
    product/panel_credential_vault.cpp
    product/panel_deployment_identity_gate.cpp
    product/panel_audit_store.cpp
    product/panel_config_store.cpp
    product/panel_console_session.cpp
    product/panel_local_server.cpp
    product/panel_product_runtime.cpp
    product/panel_service_bridge.cpp
    product/panel_system_information.cpp
    product/panel_worker.cpp
    product/product_account_port.cpp
    product/product_network_settings_port.cpp
    product/product_remote_control_port.cpp
    product/product_security_records_port.cpp
    product/product_settings_port.cpp
    product/product_voice_call_consent_overlay.cpp
    product/windows_environment_probe.cpp
    ${PX_PRODUCT_GENERATED_DIR}/icon.rc
)

if(PX_CAPABILITY_CLOUD_APP_CATALOG)
    target_sources(px_panel PRIVATE product/product_cloud_applications_port.cpp)
endif()
if(PX_CAPABILITY_DESKTOP_HOST)
    target_sources(px_panel PRIVATE product/product_server_status_port.cpp)
endif()
if(PX_CAPABILITY_SYSTEM_INFORMATION AND NOT PX_CAPABILITY_DESKTOP_HOST)
    target_sources(px_panel PRIVATE product/panel_os_info_supervisor.cpp)
endif()

target_include_directories(px_panel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/product)
set_target_properties(px_panel PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
target_link_options(px_panel PRIVATE "/ENTRY:mainCRTStartup")
if(MSVC)
    # RelWithDebInfo enables /DEBUG, whose linker default retains otherwise
    # unreachable COMDATs. Panel does not use DesktopShell's media presentation
    # methods, so remove those paths and their static FFmpeg/SDK dependencies.
    target_link_options(px_panel PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>" "$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>")
endif()

target_link_libraries(px_panel PRIVATE
    px_panel_imgui_views
    px_common
    px_console_client
    px_relay_client
    px_cp_message
    px_server_message
    px_rp_message
    px_message
    unofficial::sqlite3::sqlite3
    OpenSSL::Crypto
    Advapi32.lib
    Ole32.lib
    PowrProf.lib
)
pixels_stage_desktop_images(px_panel)

set_target_properties(px_panel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}")

add_custom_command(TARGET px_panel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel>/resources/icons"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel>/resources/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_panel>/resources/fonts/Roboto-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_deps/px_imgui/misc/fonts/Roboto-Medium.ttf"
        "$<TARGET_FILE_DIR:px_panel>/resources/fonts/Roboto-Medium.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/web/px_web_client/src/assets/px_icon.png"
        "$<TARGET_FILE_DIR:px_panel>/resources/icons/px_icon.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/icons/LICENSE.lucide"
        "$<TARGET_FILE_DIR:px_panel>/resources/licenses/Lucide.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/libplacebo/lib/libplacebo-349.dll"
        "$<TARGET_FILE_DIR:px_panel>/libplacebo-349.dll"
)
target_link_options(px_panel_imgui_preview PRIVATE "/ENTRY:mainCRTStartup")
pixels_stage_desktop_images(px_panel_imgui_preview)

add_executable(px_ui_component_gallery
    component_gallery.cpp
    component_gallery_main.cpp
)
target_link_libraries(px_ui_component_gallery PRIVATE px_panel_desktop_shell px_ui px_imgui SDL3::SDL3-static)
set_target_properties(px_ui_component_gallery PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}"
    WIN32_EXECUTABLE TRUE
)
target_link_options(px_ui_component_gallery PRIVATE "/ENTRY:mainCRTStartup")
pixels_stage_desktop_images(px_ui_component_gallery)

add_custom_command(TARGET px_panel_imgui_preview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts/Roboto-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_deps/px_imgui/misc/fonts/Roboto-Medium.ttf"
        "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/fonts/Roboto-Medium.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/web/px_web_client/src/assets/px_icon.png"
        "$<TARGET_FILE_DIR:px_panel_imgui_preview>/resources/icons/px_icon.png"
)

add_custom_command(TARGET px_ui_component_gallery POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_ui_component_gallery>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_ui_component_gallery>/resources/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_ui_component_gallery>/resources/fonts/Roboto-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_deps/px_imgui/misc/fonts/Roboto-Medium.ttf"
        "$<TARGET_FILE_DIR:px_ui_component_gallery>/resources/fonts/Roboto-Medium.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/web/px_web_client/src/assets/px_icon.png"
        "$<TARGET_FILE_DIR:px_ui_component_gallery>/resources/icons/px_icon.png"
)

if(BUILD_TESTING AND TARGET GTest::gtest)
    add_executable(px_panel_product_tests
        product/tests/panel_product_tests.cpp
        product/environment_diagnostics.cpp
        product/panel_audit_store.cpp
        product/connection_progress_tracker.cpp
        product/panel_connection_input.cpp
        product/panel_connection_links.cpp
        product/panel_device_name.cpp
        product/panel_local_server.cpp
        product/panel_system_information.cpp
        product/panel_config_store.cpp
        product/panel_worker.cpp
        product/windows_environment_probe.cpp
    )
    target_include_directories(px_panel_product_tests BEFORE PRIVATE "${PX_PRODUCT_GENERATED_DIR}")
    target_include_directories(px_panel_product_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/product)
    target_link_libraries(px_panel_product_tests PRIVATE
        px_common
        px_cp_message
        px_ui
        px_rp_message
        unofficial::sqlite3::sqlite3
        OpenSSL::Crypto
        GTest::gtest
        GTest::gtest_main
        Advapi32.lib
        Ole32.lib
        PowrProf.lib
    )
    add_test(NAME px_panel_product_tests COMMAND px_panel_product_tests)
    if(PX_CAPABILITY_SYSTEM_INFORMATION AND NOT PX_CAPABILITY_DESKTOP_HOST)
        add_executable(px_panel_os_info_test_child product/tests/os_info_test_child.cpp)
        set_target_properties(px_panel_os_info_test_child PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/os_info_test_runtime"
        )
        target_sources(px_panel_product_tests PRIVATE
            product/panel_os_info_supervisor.cpp
            product/tests/panel_os_info_supervisor_tests.cpp
        )
        add_dependencies(px_panel_product_tests px_panel_os_info_test_child)
        set_tests_properties(px_panel_product_tests PROPERTIES
            ENVIRONMENT "PIXELS_OS_INFO_TEST_CHILD=$<TARGET_FILE:px_panel_os_info_test_child>"
        )
    endif()
endif()
