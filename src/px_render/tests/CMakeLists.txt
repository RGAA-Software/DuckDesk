find_package(GTest CONFIG REQUIRED)

add_executable(test_render_architecture_core
    unit/test_render_architecture_core.cpp)
target_link_libraries(test_render_architecture_core
    PRIVATE render_architecture_core GTest::gtest GTest::gtest_main)
add_test(NAME render_architecture_core COMMAND test_render_architecture_core)

add_executable(test_captured_media_pipeline
    unit/test_captured_media_pipeline.cpp)
target_link_libraries(test_captured_media_pipeline
    PRIVATE render_architecture_core GTest::gtest GTest::gtest_main)
add_test(NAME captured_media_pipeline COMMAND test_captured_media_pipeline)

add_executable(test_network_transport_hub
    unit/test_network_transport_hub.cpp)
target_link_libraries(test_network_transport_hub
    PRIVATE render_architecture_core GTest::gtest GTest::gtest_main)
add_test(NAME network_transport_hub COMMAND test_network_transport_hub)

add_executable(test_ws_callback_workflow
    unit/test_ws_callback_workflow.cpp)
target_link_libraries(test_ws_callback_workflow
    PRIVATE px_common_new GTest::gtest GTest::gtest_main)
add_test(NAME ws_callback_workflow COMMAND test_ws_callback_workflow)

add_executable(test_media_recorder_sink
    unit/test_media_recorder_sink.cpp)
target_link_libraries(test_media_recorder_sink
    PRIVATE render_media_recorder GTest::gtest GTest::gtest_main)
add_test(NAME media_recorder_sink COMMAND test_media_recorder_sink)

add_executable(test_live_pusher_sink
    unit/test_live_pusher_sink.cpp)
target_link_libraries(test_live_pusher_sink
    PRIVATE render_live_pusher GTest::gtest GTest::gtest_main)
add_test(NAME live_pusher_sink COMMAND test_live_pusher_sink)

add_executable(test_live_pusher_ffmpeg
    ../architecture/sinks/live_pusher/tests/test_live_pusher_ffmpeg.cpp)
target_include_directories(test_live_pusher_ffmpeg PRIVATE
    ../architecture/sinks/live_pusher)
target_link_libraries(test_live_pusher_ffmpeg
    PRIVATE render_live_pusher GTest::gtest GTest::gtest_main)
add_test(NAME live_pusher_ffmpeg COMMAND test_live_pusher_ffmpeg)

add_executable(test_frame_resizer_processor
    unit/test_frame_resizer_processor.cpp)
target_link_libraries(test_frame_resizer_processor
    PRIVATE render_frame_processors GTest::gtest GTest::gtest_main)
add_test(NAME frame_resizer_processor COMMAND test_frame_resizer_processor)

add_executable(test_frame_carrier_processor
    unit/test_frame_carrier_processor.cpp)
target_link_libraries(test_frame_carrier_processor
    PRIVATE render_frame_processors GTest::gtest GTest::gtest_main)
add_test(NAME frame_carrier_processor COMMAND test_frame_carrier_processor)

add_executable(test_opus_encoder_processor
    unit/test_opus_encoder_processor.cpp)
target_link_libraries(test_opus_encoder_processor
    PRIVATE render_audio_processors GTest::gtest GTest::gtest_main)
add_test(NAME opus_encoder_processor COMMAND test_opus_encoder_processor)

add_executable(test_opus_encoder_runtime
    ../architecture/encoders/opus/tests/test_opus_encoder_runtime.cpp)
target_include_directories(test_opus_encoder_runtime PRIVATE
    ../architecture/encoders/opus)
target_link_libraries(test_opus_encoder_runtime
    PRIVATE render_audio_processors GTest::gtest GTest::gtest_main)
add_test(NAME opus_encoder_runtime COMMAND test_opus_encoder_runtime)

add_executable(test_input_replay_service
    unit/test_input_replay_service.cpp)
target_link_libraries(test_input_replay_service
    PRIVATE render_domain_services GTest::gtest GTest::gtest_main)
add_test(NAME input_replay_service COMMAND test_input_replay_service)

add_executable(test_joystick_service
    unit/test_joystick_service.cpp)
target_link_libraries(test_joystick_service
    PRIVATE render_domain_services GTest::gtest GTest::gtest_main)
add_test(NAME joystick_service COMMAND test_joystick_service)

add_executable(test_file_transfer_service
    unit/test_file_transfer_service.cpp)
target_link_libraries(test_file_transfer_service
    PRIVATE render_domain_services GTest::gtest GTest::gtest_main)
add_test(NAME file_transfer_service COMMAND test_file_transfer_service)

add_executable(test_was_audio_capture_source
    unit/test_was_audio_capture_source.cpp)
target_include_directories(test_was_audio_capture_source PRIVATE
    ../architecture/sources/was_audio)
target_link_libraries(test_was_audio_capture_source
    PRIVATE render_audio_sources GTest::gtest GTest::gtest_main)
add_test(NAME was_audio_capture_source COMMAND test_was_audio_capture_source)

add_executable(test_was_audio_capture_runtime
    ../architecture/sources/was_audio/tests/test_was_audio_capture_runtime.cpp)
target_include_directories(test_was_audio_capture_runtime PRIVATE
    ../architecture/sources/was_audio)
target_link_libraries(test_was_audio_capture_runtime PRIVATE
    render_audio_sources)
add_test(NAME was_audio_capture_runtime COMMAND test_was_audio_capture_runtime)

add_executable(test_was_audio_capture_hardware
    ../architecture/sources/was_audio/tests/test_miniaudio_audio_capture.cpp
    ../architecture/sources/was_audio/miniaudio_audio_capture.cpp
    ../architecture/sources/was_audio/third_party/miniaudio/miniaudio.c
    ../architecture/sources/was_audio/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp)
target_include_directories(test_was_audio_capture_hardware PRIVATE
    ../architecture/sources/was_audio)
target_link_libraries(test_was_audio_capture_hardware PRIVATE
    px_common_new ole32 mmdevapi)
target_compile_definitions(test_was_audio_capture_hardware PRIVATE
    PX_AUDIO_CAPTURE_TESTING=1)
add_test(NAME was_audio_capture_hardware
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/scripts/run_render_hardware_test.ps1"
        -Kind was-default
        -Executable "$<TARGET_FILE:test_was_audio_capture_hardware>")
set_tests_properties(was_audio_capture_hardware PROPERTIES
    LABELS "render-hardware"
    SKIP_RETURN_CODE 125
    TIMEOUT 45)

add_executable(test_miniaudio_reinit_cancel
    ../architecture/sources/was_audio/tests/test_miniaudio_reinit_cancel.cpp
    ../architecture/sources/was_audio/miniaudio_audio_capture.cpp
    ../architecture/sources/was_audio/third_party/miniaudio/miniaudio.c
    ../architecture/sources/was_audio/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp)
target_include_directories(test_miniaudio_reinit_cancel PRIVATE
    ../architecture/sources/was_audio)
target_link_libraries(test_miniaudio_reinit_cancel PRIVATE
    px_common_new ole32 mmdevapi)
target_compile_definitions(test_miniaudio_reinit_cancel PRIVATE
    PX_AUDIO_CAPTURE_TESTING=1)
add_test(NAME miniaudio_reinit_cancel COMMAND test_miniaudio_reinit_cancel)

add_executable(test_process_loopback_lifecycle
    ../architecture/sources/was_audio/tests/test_process_loopback_lifecycle.cpp)
target_include_directories(test_process_loopback_lifecycle PRIVATE
    ../architecture/sources/was_audio)
target_link_libraries(test_process_loopback_lifecycle PRIVATE
    render_audio_sources)
add_test(NAME process_loopback_lifecycle COMMAND test_process_loopback_lifecycle)

add_executable(test_miniaudio_pid_loopback
    ../architecture/sources/was_audio/tests/test_miniaudio_pid_loopback.cpp)
target_include_directories(test_miniaudio_pid_loopback PRIVATE
    ../architecture/sources/was_audio)
target_link_libraries(test_miniaudio_pid_loopback PRIVATE
    render_audio_sources)
add_test(NAME miniaudio_pid_loopback_hardware
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/scripts/run_render_hardware_test.ps1"
        -Kind process-loopback
        -Executable "$<TARGET_FILE:test_miniaudio_pid_loopback>")
set_tests_properties(miniaudio_pid_loopback_hardware PROPERTIES
    LABELS "render-hardware"
    SKIP_RETURN_CODE 125
    TIMEOUT 45)

add_executable(test_render_service_rpc_state test_render_service_rpc_state.cpp)
target_link_libraries(test_render_service_rpc_state
    PRIVATE px_common_new GTest::gtest GTest::gtest_main)
add_test(NAME render_service_rpc_state COMMAND test_render_service_rpc_state)

add_executable(test_logical_session_registry
    test_logical_session_registry.cpp
    ../session/logical_session_registry.cpp)
target_link_libraries(test_logical_session_registry
    PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME logical_session_registry COMMAND test_logical_session_registry)

add_executable(test_direct_session_grant_store
    test_direct_session_grant_store.cpp
    ../network/ws/direct_session_grant_store.cpp)
target_link_libraries(test_direct_session_grant_store
    PRIVATE px_common_new GTest::gtest GTest::gtest_main)
add_test(NAME direct_session_grant_store COMMAND test_direct_session_grant_store)

add_executable(test_plugin_context_lifecycle test_plugin_context_lifecycle.cpp)
target_link_libraries(test_plugin_context_lifecycle
    PRIVATE px_common_new px_plugin GTest::gtest GTest::gtest_main)
add_test(NAME plugin_context_lifecycle COMMAND test_plugin_context_lifecycle)

add_executable(test_webrtc_libraries_lifecycle
        test_webrtc_libraries_lifecycle.cpp
        ../network/webrtc_library_host.cpp)
target_link_libraries(test_webrtc_libraries_lifecycle
        PRIVATE px_common_new px_net_plugin px_plugin
        GTest::gtest GTest::gtest_main)
target_compile_definitions(test_webrtc_libraries_lifecycle
        PRIVATE
        PX_WEBRTC_REMOTE_LIBRARY_PATH="$<TARGET_FILE:net_rtc>"
        PX_WEBRTC_LOCAL_LIBRARY_PATH="$<TARGET_FILE:net_rtc_local>")
add_dependencies(test_webrtc_libraries_lifecycle net_rtc net_rtc_local)
add_test(NAME webrtc_libraries_lifecycle
    COMMAND test_webrtc_libraries_lifecycle)

add_executable(test_render_builtin_linkage unit/test_render_builtin_linkage.cpp)
target_link_libraries(test_render_builtin_linkage PRIVATE
    cap_dda cap_gdi enc_ffmpeg enc_amf enc_nvenc net_ws net_udp net_relay
    GTest::gtest GTest::gtest_main)
add_test(NAME render_builtin_linkage COMMAND test_render_builtin_linkage)

set_tests_properties(
    render_architecture_core
    captured_media_pipeline
    network_transport_hub
    ws_callback_workflow
    frame_resizer_processor
    frame_carrier_processor
    opus_encoder_processor
    input_replay_service
    joystick_service
    file_transfer_service
    render_service_rpc_state
    logical_session_registry
    direct_session_grant_store
    PROPERTIES LABELS "render-unit")

set_property(TEST
    captured_media_pipeline
    ws_callback_workflow
    media_recorder_sink
    live_pusher_sink
    was_audio_capture_source
    was_audio_capture_runtime
    miniaudio_reinit_cancel
    process_loopback_lifecycle
    plugin_context_lifecycle
    webrtc_libraries_lifecycle
    APPEND PROPERTY LABELS "render-lifecycle")

set_property(TEST
    live_pusher_ffmpeg
    opus_encoder_runtime
    render_builtin_linkage
    APPEND PROPERTY LABELS "render-integration")
