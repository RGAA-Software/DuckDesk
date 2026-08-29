project(cap_was_audio)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_language(C)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/was_audio_capture_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/was_audio_capture_runtime.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/process_loopback_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin ole32 mmdevapi)

# Archived WASAPI implementation (not built): archive/wasapi_audio_capture.*

# Local smoke test: loopback capture while playing a tone.
add_executable(test_plugin_was_audio_capture
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp
)
target_include_directories(test_plugin_was_audio_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_plugin_was_audio_capture PRIVATE px_common_new ole32 mmdevapi)
target_compile_definitions(test_plugin_was_audio_capture PRIVATE PX_AUDIO_CAPTURE_TESTING=1)

add_executable(test_miniaudio_reinit_cancel
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_miniaudio_reinit_cancel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp
)
target_include_directories(test_miniaudio_reinit_cancel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_miniaudio_reinit_cancel PRIVATE px_common_new ole32 mmdevapi)
target_compile_definitions(test_miniaudio_reinit_cancel PRIVATE PX_AUDIO_CAPTURE_TESTING=1)

add_executable(test_was_audio_capture_runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_was_audio_capture_runtime.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/was_audio_capture_runtime.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/process_loopback_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp
)
target_include_directories(test_was_audio_capture_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_was_audio_capture_runtime PRIVATE
        px_common_new px_message px_plugin ole32 mmdevapi)

# Diagnose MiniAudio PID loopback vs ActivateAudioInterfaceAsync.
add_executable(test_miniaudio_pid_loopback
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_miniaudio_pid_loopback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_audio_capture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp
)
target_include_directories(test_miniaudio_pid_loopback PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_miniaudio_pid_loopback PRIVATE px_common_new ole32 mmdevapi)
