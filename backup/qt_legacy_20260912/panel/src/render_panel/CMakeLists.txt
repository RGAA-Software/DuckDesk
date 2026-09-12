set(PANEL_IMGUI_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/network/http_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/ws_panel_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/udp_broadcaster.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/px_service_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/render_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/px_console_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/records_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/records_ticket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/records_http_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/record_transfer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/panel_rtc_config_refresh_gate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/network_settings_workflow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/panel_network_settings_controller.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/database/px_database.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/database/visit_record.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/database/visit_record_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/database/file_transfer_record.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/database/file_transfer_record_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/database/stream_db_operator.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/px_application.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/panel_shutdown_sequence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_statistics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_system_monitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_render_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_running_pipe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_connected_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/px_render_msg_processor.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/devices/app_stream_list.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/stream_resource_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/stream_resource_refresh_gate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/stream_launch_auth_workflow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/running_stream_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/stream_state_checker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/stream_messages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/px_device_manager.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/util/conn_info_parser.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/clipboard/panel_clipboard_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clipboard/win/panel_cp_file_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clipboard/win/panel_cp_virtual_file.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/console_scanner/console_datagram_receiver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console_scanner/console_scanner.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/user/px_user_manager.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/upgrade/upgrade_helper.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/console/px_console_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/console_error_presenter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/console/px_event_manager.cpp

)

add_subdirectory(companion)

set(PANEL_IMGUI_SRCS ${PANEL_IMGUI_SRCS} ${COMPANION_SRCS} PARENT_SCOPE)
