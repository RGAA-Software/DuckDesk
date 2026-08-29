project(media_recorder)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/media_recorder_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/media_recorder_runtime.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin px_media_record_new)

if(TESTS_ENABLED)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_media_recorder_runtime
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_media_recorder_runtime.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/media_recorder_runtime.cpp)
    target_include_directories(test_media_recorder_runtime PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_media_recorder_runtime PRIVATE
            px_common_new px_message px_plugin px_media_record_new
            GTest::gtest GTest::gtest_main)
endif()
