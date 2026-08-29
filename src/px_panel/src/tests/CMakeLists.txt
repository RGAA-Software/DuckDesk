cmake_minimum_required(VERSION 3.26)

find_package(GTest CONFIG REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network)

#add_executable(test_db
#        test_db.cpp
#        ../db/db_game.cpp
#        ../db/db_game_manager.cpp
#        ../px_context.cpp
#        ../network/ip_util.cpp
#)
#target_link_libraries(test_db PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main sqlite_orm px_common_new px_steam_manager_new px_encoder_new)
#target_link_libraries(test_db PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Core5Compat Qt6::WebSockets Qt6::Network)
#
#add_executable(test_process
#        test_process.cpp
#)
#target_link_libraries(test_process PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main px_common_new px_steam_manager_new px_encoder_new)

add_executable(test_resolutions test_resolutions.cpp)

add_executable(test_http_client test_http_client.cpp)
target_link_libraries(test_http_client px_common_new)

add_executable(test_access_decrypt test_access_decrypt.cpp)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(test_access_decrypt PRIVATE OpenSSL::Crypto nlohmann_json::nlohmann_json)

add_executable(test_records_catalog test_records_catalog.cpp
        ../render_panel/network/records_catalog.cpp)
target_link_libraries(test_records_catalog PRIVATE GTest::gtest GTest::gtest_main)

add_executable(test_records_ticket test_records_ticket.cpp
        ../render_panel/network/records_ticket.cpp)
target_link_libraries(test_records_ticket PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto)

add_executable(test_record_transfer test_record_transfer.cpp
        ../render_panel/network/record_transfer.cpp)
target_link_libraries(test_record_transfer PRIVATE GTest::gtest GTest::gtest_main)

add_executable(test_sqlite_storage_config test_sqlite_storage_config.cpp)
target_link_libraries(test_sqlite_storage_config PRIVATE
        GTest::gtest GTest::gtest_main sqlite_orm::sqlite_orm unofficial::sqlite3::sqlite3)

add_executable(test_connection_policy test_connection_policy.cpp)
target_link_libraries(test_connection_policy PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME panel_connection_policy COMMAND test_connection_policy)

add_executable(test_console_api_error test_console_api_error.cpp)
target_link_libraries(test_console_api_error PRIVATE
        GTest::gtest GTest::gtest_main px_console_client px_common_new)
add_test(NAME panel_console_api_error COMMAND test_console_api_error)

add_executable(test_stream_item_display test_stream_item_display.cpp)
target_link_libraries(test_stream_item_display PRIVATE
        GTest::gtest GTest::gtest_main px_console_client px_common_new)
add_test(NAME panel_stream_item_display COMMAND test_stream_item_display)

add_executable(test_panel_rtc_config_refresh_gate
        test_panel_rtc_config_refresh_gate.cpp
        ../render_panel/network/panel_rtc_config_refresh_gate.cpp)
target_link_libraries(test_panel_rtc_config_refresh_gate PRIVATE
        GTest::gtest GTest::gtest_main)
add_test(NAME panel_rtc_config_refresh_gate COMMAND test_panel_rtc_config_refresh_gate)
