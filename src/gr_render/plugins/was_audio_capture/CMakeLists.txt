project(plugin_was_audio_capture)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_language(C)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/was_audio_capture_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE tc_common_new tc_message tc_plugin)

# Archived WASAPI implementation (not built): archive/wasapi_audio_capture.*

# Local smoke test: loopback capture while playing a tone.
add_executable(test_plugin_was_audio_capture
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
)
target_include_directories(test_plugin_was_audio_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_plugin_was_audio_capture PRIVATE tc_common_new)
