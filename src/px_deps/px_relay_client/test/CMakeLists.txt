cmake_minimum_required(VERSION 3.22)
find_package(GTest CONFIG REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Core5Compat)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Core5Compat)

add_executable(test_asio_ws_client test_asio_client.cpp)
target_link_libraries(test_asio_ws_client PRIVATE
    px_common_new px_relay_client px_relay_message
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main Qt6::Core Qt6::Core5Compat)

add_executable(test_relay_client_sdk_lifecycle
    test_relay_client_sdk_lifecycle.cpp)
target_link_libraries(test_relay_client_sdk_lifecycle PRIVATE
    px_common_new px_relay_client px_relay_message
    GTest::gtest GTest::gtest_main Qt6::Core Qt6::Core5Compat)

add_executable(test_relay_ws_reconnect test_relay_ws_reconnect.cpp)
target_link_libraries(test_relay_ws_reconnect PRIVATE
    px_common_new px_relay_client px_relay_message
    GTest::gtest GTest::gtest_main Qt6::Core Qt6::Core5Compat)
add_test(NAME relay_ws_reconnect COMMAND test_relay_ws_reconnect)
set_tests_properties(relay_ws_reconnect PROPERTIES LABELS "render-lifecycle")
