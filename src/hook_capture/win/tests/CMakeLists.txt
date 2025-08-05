cmake_minimum_required(VERSION 3.26)

find_package(GTest CONFIG REQUIRED)

add_executable(test_audio_capture test_audio_capture.cpp)
target_link_libraries(test_audio_capture PRIVATE tc_common_new tc_capture_new GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)

#add_custom_command(TARGET test_encoder POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/test.png"  "${CMAKE_CURRENT_BINARY_DIR}"
#)