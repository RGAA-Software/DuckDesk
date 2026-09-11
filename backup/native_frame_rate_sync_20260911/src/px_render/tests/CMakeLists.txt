find_package(GTest CONFIG REQUIRED)

add_executable(test_render_ui_task_queue unit/test_ui_task_queue.cpp)
add_executable(test_rdp_route_close unit/test_rdp_route_close.cpp)
target_link_libraries(test_rdp_route_close PRIVATE net_ws render_architecture_core px_rdp_stream GTest::gtest GTest::gtest_main)
add_test(NAME rdp_route_close COMMAND test_rdp_route_close)
set_tests_properties(rdp_route_close PROPERTIES TIMEOUT 30)
target_include_directories(test_render_ui_task_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_render_ui_task_queue PRIVATE GTest::gtest GTest::gtest_main user32)
add_test(NAME render_ui_task_queue COMMAND test_render_ui_task_queue)
set_tests_properties(render_ui_task_queue PROPERTIES TIMEOUT 15)

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
    PRIVATE px_common GTest::gtest GTest::gtest_main)
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
    px_common ole32 mmdevapi)
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
    px_common ole32 mmdevapi)
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
    PRIVATE px_common GTest::gtest GTest::gtest_main)
add_test(NAME render_service_rpc_state COMMAND test_render_service_rpc_state)

add_executable(test_logical_session_registry
    test_logical_session_registry.cpp
    ../session/logical_session_registry.cpp)
target_link_libraries(test_logical_session_registry
    PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME logical_session_registry COMMAND test_logical_session_registry)
add_executable(test_application_text_service test_application_text_service.cpp
    ../ingress/application_text_service.cpp ../session/logical_session_registry.cpp)
target_link_libraries(test_application_text_service PRIVATE px_message px_ft_engine GTest::gtest GTest::gtest_main)
add_test(NAME application_text_service COMMAND test_application_text_service)

add_executable(test_direct_session_grant_store
    test_direct_session_grant_store.cpp
    ../network/ws/direct_session_grant_store.cpp)
target_link_libraries(test_direct_session_grant_store
    PRIVATE px_common GTest::gtest GTest::gtest_main)
add_test(NAME direct_session_grant_store COMMAND test_direct_session_grant_store)

add_executable(test_render_execution_context_lifecycle test_render_execution_context_lifecycle.cpp)
target_link_libraries(test_render_execution_context_lifecycle PRIVATE render_architecture_core GTest::gtest GTest::gtest_main)
add_test(NAME render_execution_context_lifecycle COMMAND test_render_execution_context_lifecycle)

add_executable(test_udp_transport_shutdown test_udp_transport_shutdown.cpp)
target_link_libraries(test_udp_transport_shutdown PRIVATE
    net_udp render_architecture_core GTest::gtest GTest::gtest_main)
add_test(NAME udp_transport_shutdown COMMAND test_udp_transport_shutdown)
add_executable(test_native_udp_voice test_native_udp_voice.cpp)
set_target_properties(test_native_udp_voice PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(test_native_udp_voice PRIVATE net_udp px_sdk_core render_architecture_core GTest::gtest GTest::gtest_main)
add_test(NAME native_udp_voice COMMAND test_native_udp_voice)

add_executable(test_relay_transport_reconnect_owner test_relay_transport_reconnect_owner.cpp)
target_link_libraries(test_relay_transport_reconnect_owner PRIVATE
    net_relay render_architecture_core GTest::gtest GTest::gtest_main)
add_test(NAME relay_transport_reconnect_owner COMMAND test_relay_transport_reconnect_owner)

add_executable(test_webrtc_transport_lifecycle
        test_webrtc_transport_lifecycle.cpp
        ../network/webrtc/webrtc_execution_context.cpp
        ../network/webrtc_transport_host.cpp)
target_link_libraries(test_webrtc_transport_lifecycle
        PRIVATE px_common net_rtc net_rtc_local
        GTest::gtest GTest::gtest_main)
target_compile_definitions(test_webrtc_transport_lifecycle
        PRIVATE
        PX_WEBRTC_REMOTE_LIBRARY_PATH="$<TARGET_FILE:net_rtc>"
        PX_WEBRTC_LOCAL_LIBRARY_PATH="$<TARGET_FILE:net_rtc_local>")
add_custom_command(TARGET test_webrtc_transport_lifecycle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:net_rtc>
        $<TARGET_FILE_DIR:test_webrtc_transport_lifecycle>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:net_rtc_local>
        $<TARGET_FILE_DIR:test_webrtc_transport_lifecycle>)
add_test(NAME webrtc_transport_lifecycle
    COMMAND test_webrtc_transport_lifecycle)

add_executable(test_rtc_heartbeat_watchdog test_rtc_heartbeat_watchdog.cpp)
target_link_libraries(test_rtc_heartbeat_watchdog PRIVATE GTest::gtest GTest::gtest_main)
add_test(NAME rtc_heartbeat_watchdog COMMAND test_rtc_heartbeat_watchdog)

add_executable(test_rtc_payload_authorization
    test_rtc_payload_authorization.cpp
    ../network/webrtc/remote/rtc_payload_authorization.cpp)
target_link_libraries(test_rtc_payload_authorization PRIVATE px_message GTest::gtest GTest::gtest_main)
add_test(NAME rtc_payload_authorization COMMAND test_rtc_payload_authorization)

add_executable(test_rtc_candidate_sdp
    test_rtc_candidate_sdp.cpp
    ../network/webrtc/local/rtc_candidate_sdp.cpp)
target_link_libraries(test_rtc_candidate_sdp PRIVATE px_common GTest::gtest GTest::gtest_main)
add_test(NAME rtc_candidate_sdp COMMAND test_rtc_candidate_sdp)

add_executable(test_render_builtin_linkage unit/test_render_builtin_linkage.cpp)
if(WIN32)
    add_executable(test_render_context_shutdown unit/test_render_context_shutdown.cpp)
    target_link_libraries(test_render_context_shutdown PRIVATE $<TARGET_PROPERTY:px_render,LINK_LIBRARIES> GTest::gtest GTest::gtest_main)
    target_link_directories(test_render_context_shutdown PRIVATE ${VCPKG_ROOT}/installed/x64-windows/lib)
    add_test(NAME render_context_shutdown COMMAND test_render_context_shutdown)
    set_tests_properties(render_context_shutdown PROPERTIES TIMEOUT 30 LABELS "render-lifecycle"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/dist")
    add_executable(test_game_process_identity unit/test_game_process_identity.cpp)
    target_link_libraries(test_game_process_identity PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME game_process_identity COMMAND test_game_process_identity)
    add_executable(owned_game_fixture unit/owned_game_fixture.cpp)
    add_executable(game_d3d11_fixture unit/game_d3d11_fixture.cpp)
    target_link_libraries(game_d3d11_fixture PRIVATE d3d11 dxgi user32)
    target_include_directories(owned_game_fixture PRIVATE ${CMAKE_SOURCE_DIR}/src/px_deps)
    set_target_properties(owned_game_fixture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/owned process fixtures"
        OUTPUT_NAME "owned game fixture")
    add_executable(test_game_owned_process unit/test_game_owned_process.cpp ../app/win/owned_game_process.cpp)
    target_include_directories(test_game_owned_process PRIVATE ${CMAKE_SOURCE_DIR}/src/px_deps)
    target_compile_definitions(test_game_owned_process PRIVATE GAME_OWNERSHIP_FIXTURE="$<TARGET_FILE:owned_game_fixture>")
    target_link_libraries(test_game_owned_process PRIVATE GTest::gtest GTest::gtest_main Wtsapi32 Userenv Advapi32)
    add_dependencies(test_game_owned_process owned_game_fixture)
    add_test(NAME game_owned_process COMMAND test_game_owned_process)
    set_tests_properties(game_process_identity game_owned_process PROPERTIES TIMEOUT 30 LABELS "render-unit")
endif()
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
    rtc_heartbeat_watchdog
    rtc_payload_authorization
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
    render_execution_context_lifecycle
    udp_transport_shutdown
    relay_transport_reconnect_owner
    webrtc_transport_lifecycle
    APPEND PROPERTY LABELS "render-lifecycle")

set_property(TEST
    live_pusher_ffmpeg
    opus_encoder_runtime
    render_builtin_linkage
    APPEND PROPERTY LABELS "render-integration")
