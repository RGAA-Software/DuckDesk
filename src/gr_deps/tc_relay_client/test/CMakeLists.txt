cmake_minimum_required(VERSION 3.22)
find_package(GTest CONFIG REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Core5Compat)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Core5Compat)

add_executable(test_asio_ws_client test_asio_client.cpp)
target_link_libraries(test_asio_ws_client PRIVATE
    tc_common_new tc_relay_client tc_relay_message
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main Qt6::Core Qt6::Core5Compat)