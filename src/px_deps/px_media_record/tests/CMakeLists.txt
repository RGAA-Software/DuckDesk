cmake_minimum_required(VERSION 3.22)
find_package(GTest CONFIG REQUIRED)

add_executable(test_record_writer test_record_writer.cpp)
target_link_libraries(test_record_writer PRIVATE px_media_record GTest::gtest GTest::gtest_main)
add_test(NAME render_record_writer COMMAND test_record_writer)
set_tests_properties(render_record_writer PROPERTIES
    LABELS "render-integration")
