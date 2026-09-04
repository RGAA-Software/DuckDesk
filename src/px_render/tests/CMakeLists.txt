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
    ../plugins/live_pusher/tests/test_live_pusher_ffmpeg.cpp)
target_include_directories(test_live_pusher_ffmpeg PRIVATE
    ../plugins/live_pusher)
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
    ../plugins/opus_encoder/tests/test_opus_encoder_runtime.cpp)
target_include_directories(test_opus_encoder_runtime PRIVATE
    ../plugins/opus_encoder)
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
    ../plugins/was_audio_capture)
target_link_libraries(test_was_audio_capture_source
    PRIVATE render_audio_sources GTest::gtest GTest::gtest_main)
add_test(NAME was_audio_capture_source COMMAND test_was_audio_capture_source)

add_executable(test_was_audio_capture_runtime
    ../plugins/was_audio_capture/tests/test_was_audio_capture_runtime.cpp)
target_include_directories(test_was_audio_capture_runtime PRIVATE
    ../plugins/was_audio_capture)
target_link_libraries(test_was_audio_capture_runtime PRIVATE
    render_audio_sources)
add_test(NAME was_audio_capture_runtime COMMAND test_was_audio_capture_runtime)

add_executable(test_plugin_was_audio_capture
    ../plugins/was_audio_capture/tests/test_miniaudio_audio_capture.cpp
    ../plugins/was_audio_capture/miniaudio_audio_capture.cpp
    ../plugins/was_audio_capture/third_party/miniaudio/miniaudio.c
    ../plugins/was_audio_capture/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp)
target_include_directories(test_plugin_was_audio_capture PRIVATE
    ../plugins/was_audio_capture)
target_link_libraries(test_plugin_was_audio_capture PRIVATE
    px_common_new ole32 mmdevapi)
target_compile_definitions(test_plugin_was_audio_capture PRIVATE
    PX_AUDIO_CAPTURE_TESTING=1)

add_executable(test_miniaudio_reinit_cancel
    ../plugins/was_audio_capture/tests/test_miniaudio_reinit_cancel.cpp
    ../plugins/was_audio_capture/miniaudio_audio_capture.cpp
    ../plugins/was_audio_capture/third_party/miniaudio/miniaudio.c
    ../plugins/was_audio_capture/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp)
target_include_directories(test_miniaudio_reinit_cancel PRIVATE
    ../plugins/was_audio_capture)
target_link_libraries(test_miniaudio_reinit_cancel PRIVATE
    px_common_new ole32 mmdevapi)
target_compile_definitions(test_miniaudio_reinit_cancel PRIVATE
    PX_AUDIO_CAPTURE_TESTING=1)

add_executable(test_process_loopback_lifecycle
    ../plugins/was_audio_capture/tests/test_process_loopback_lifecycle.cpp)
target_include_directories(test_process_loopback_lifecycle PRIVATE
    ../plugins/was_audio_capture)
target_link_libraries(test_process_loopback_lifecycle PRIVATE
    render_audio_sources)

add_executable(test_miniaudio_pid_loopback
    ../plugins/was_audio_capture/tests/test_miniaudio_pid_loopback.cpp)
target_include_directories(test_miniaudio_pid_loopback PRIVATE
    ../plugins/was_audio_capture)
target_link_libraries(test_miniaudio_pid_loopback PRIVATE
    render_audio_sources)

add_executable(test_render_service_rpc_state test_render_service_rpc_state.cpp)
target_link_libraries(test_render_service_rpc_state
    PRIVATE px_common_new GTest::gtest GTest::gtest_main)

add_executable(test_logical_session_registry
    test_logical_session_registry.cpp
    ../session/logical_session_registry.cpp)
target_link_libraries(test_logical_session_registry
    PRIVATE GTest::gtest GTest::gtest_main)

add_executable(test_direct_session_grant_store
    test_direct_session_grant_store.cpp
    ../plugins/net_ws/direct_session_grant_store.cpp)
target_link_libraries(test_direct_session_grant_store
    PRIVATE px_common_new GTest::gtest GTest::gtest_main)

add_executable(test_plugin_context_lifecycle test_plugin_context_lifecycle.cpp)
target_link_libraries(test_plugin_context_lifecycle
    PRIVATE px_common_new px_plugin GTest::gtest GTest::gtest_main)

add_executable(test_rtc_plugins_dll_lifecycle
        test_rtc_plugins_dll_lifecycle.cpp
        ../network/webrtc_library_host.cpp)
target_link_libraries(test_rtc_plugins_dll_lifecycle
        PRIVATE px_common_new px_net_plugin px_plugin
        GTest::gtest GTest::gtest_main)
target_compile_definitions(test_rtc_plugins_dll_lifecycle
        PRIVATE
        PX_RTC_PLUGIN_DLL_PATH="$<TARGET_FILE:net_rtc>"
        PX_RTC_LOCAL_PLUGIN_DLL_PATH="$<TARGET_FILE:net_rtc_local>")
add_dependencies(test_rtc_plugins_dll_lifecycle net_rtc net_rtc_local)
add_test(NAME rtc_plugins_dll_lifecycle
    COMMAND test_rtc_plugins_dll_lifecycle)

add_executable(test_render_builtin_linkage unit/test_render_builtin_linkage.cpp)
target_include_directories(test_render_builtin_linkage PRIVATE ../plugins)
target_link_libraries(test_render_builtin_linkage PRIVATE
    cap_dda cap_gdi enc_ffmpeg enc_amf enc_nvenc net_ws net_udp net_relay
    GTest::gtest GTest::gtest_main)
add_test(NAME render_builtin_linkage COMMAND test_render_builtin_linkage)
