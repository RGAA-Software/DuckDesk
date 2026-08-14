cmake_minimum_required(VERSION 3.26)

find_package(GTest CONFIG REQUIRED)
#find_package(libyuv CONFIG REQUIRED)

add_executable(test_encoder test.cpp)
target_link_libraries(test_encoder PRIVATE tc_common_new tc_encoder_new GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
#target_link_libraries(test_encoder PRIVATE yuv)

add_custom_command(TARGET test_encoder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/test.png"  "${CMAKE_BINARY_DIR}"
)