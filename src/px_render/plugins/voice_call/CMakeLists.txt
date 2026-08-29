project(voice_call)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(voice_call_runtime STATIC
    voice_call_runtime.cpp)
target_link_libraries(voice_call_runtime PUBLIC
    px_common_new px_message px_plugin px_voice_call)

add_library(${PROJECT_NAME} SHARED voice_call_plugin.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE
    voice_call_runtime)

if(TESTS_ENABLED)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_voice_call_runtime
        tests/test_voice_call_runtime.cpp)
    target_include_directories(test_voice_call_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_voice_call_runtime PRIVATE
        voice_call_runtime GTest::gtest GTest::gtest_main)
endif()
