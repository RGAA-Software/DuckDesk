project(enc_opus)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/opus_encoder_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/opus_encoder_runtime.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin px_opus_codec_new)

if(TESTS_ENABLED)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_opus_encoder_runtime
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_opus_encoder_runtime.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/opus_encoder_runtime.cpp)
    target_include_directories(test_opus_encoder_runtime PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_opus_encoder_runtime PRIVATE
            px_common_new px_message px_plugin px_opus_codec_new
            GTest::gtest GTest::gtest_main)
endif()
