project(live_pusher)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} SHARED
        live_pusher_plugin.cpp
        live_pusher_runtime.cpp
        live_pusher_ffmpeg.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_ROOT}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin ${FFMPEG_LIBRARIES})

if(TESTS_ENABLED)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_live_pusher_runtime
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_live_pusher_runtime.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/live_pusher_runtime.cpp)
    target_include_directories(test_live_pusher_runtime PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_live_pusher_runtime PRIVATE
            px_common_new px_message px_plugin GTest::gtest GTest::gtest_main)

    add_executable(test_live_pusher_ffmpeg
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_live_pusher_ffmpeg.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/live_pusher_ffmpeg.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/live_pusher_runtime.cpp)
    target_include_directories(test_live_pusher_ffmpeg PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR} ${FFMPEG_ROOT}/include)
    target_link_libraries(test_live_pusher_ffmpeg PRIVATE
            px_common_new px_message px_plugin ${FFMPEG_LIBRARIES}
            GTest::gtest GTest::gtest_main)
endif()
