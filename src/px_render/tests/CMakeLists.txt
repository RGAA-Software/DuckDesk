find_package(GTest CONFIG REQUIRED)

add_executable(test_render_service_rpc_state test_render_service_rpc_state.cpp)
target_link_libraries(test_render_service_rpc_state
    PRIVATE px_common_new GTest::gtest GTest::gtest_main)
