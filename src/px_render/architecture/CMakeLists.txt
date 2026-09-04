add_library(render_architecture_core STATIC
    diagnostics/performance_window.cpp
    diagnostics/rate_limited_log.cpp
    diagnostics/render_error.cpp
    diagnostics/transport_performance_window.cpp
    modules/render_module.cpp
    encoders/video_encoder_module.cpp
    sources/monitor_capture_source.cpp
    modules/builtin_module_catalog.cpp
    network/network_transport_hub.cpp
    observers/frame_debugger_observer.cpp
    observers/pipeline_statistics_observer.cpp
    pipeline/captured_media_pipeline.cpp
    pipeline/encoded_media_bus.cpp
    pipeline/media_types.cpp
    runtime/render_composition_root.cpp)

target_include_directories(render_architecture_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(render_architecture_core PUBLIC cxx_std_23)
target_link_libraries(render_architecture_core PUBLIC px_common_new)

add_library(render_media_recorder STATIC
    sinks/media_recorder_sink.cpp)
target_include_directories(render_media_recorder
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(render_media_recorder PUBLIC cxx_std_23)
target_link_libraries(render_media_recorder
    PUBLIC render_architecture_core px_media_record_new)

add_library(render_live_pusher STATIC
    sinks/live_pusher_sink.cpp
    sinks/live_pusher/live_pusher_ffmpeg.cpp)
target_include_directories(render_live_pusher
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sinks/live_pusher
        ${FFMPEG_ROOT}/include)
target_compile_features(render_live_pusher PUBLIC cxx_std_23)
target_link_libraries(render_live_pusher
    PUBLIC render_architecture_core
    PRIVATE ${FFMPEG_LIBRARIES})

add_library(render_frame_processors STATIC
    processors/frame_carrier_processor.cpp
    processors/frame_resizer_processor.cpp
    processors/frame_carrier/image_generator.cpp
    processors/frame_carrier/video_frame_carrier.cpp
    processors/frame_resizer/frame_render.cpp)
target_include_directories(render_frame_processors
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/processors/frame_carrier
        ${CMAKE_CURRENT_SOURCE_DIR}/processors/frame_resizer)
target_compile_features(render_frame_processors PUBLIC cxx_std_23)
target_link_libraries(render_frame_processors
    PUBLIC render_architecture_core px_common_new
    PRIVATE yuv winmm.lib SetupAPI.lib d3dcompiler.lib)

add_library(render_audio_processors STATIC
    processors/opus_encoder_processor.cpp
    encoders/opus/opus_encoder_runtime.cpp)
target_include_directories(render_audio_processors
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/encoders/opus)
target_compile_features(render_audio_processors PUBLIC cxx_std_23)
target_link_libraries(render_audio_processors
    PUBLIC render_architecture_core px_common_new
    PRIVATE px_opus_codec_new)

add_library(render_audio_sources STATIC
    sources/was_audio_capture_source.cpp
    sources/was_audio/was_audio_capture_runtime.cpp
    sources/was_audio/miniaudio_audio_capture.cpp
    sources/was_audio/process_loopback_audio_capture.cpp
    sources/was_audio/third_party/miniaudio/miniaudio.c
    sources/was_audio/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp)
target_include_directories(render_audio_sources
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sources/was_audio)
target_compile_features(render_audio_sources PUBLIC cxx_std_23)
target_link_libraries(render_audio_sources
    PUBLIC render_architecture_core px_common_new
    PRIVATE ole32 mmdevapi)

add_library(render_domain_services STATIC
    services/file_transfer_service.h
    services/file_transfer_types.h
    services/input_replay_service.cpp
    services/joystick_service.cpp
    services/voice_call_service.cpp
    services/file_transfer_service.cpp
    services/voice_call/voice_call_runtime.cpp
    services/joystick/vigem/vigem_controller.cpp
    services/joystick/vigem/sdk/ViGEmClient.cpp
    services/event_replayer/win_event_replayer.cpp)
target_include_directories(render_domain_services
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/services/event_replayer
        ${CMAKE_CURRENT_SOURCE_DIR}/services/voice_call
        ${CMAKE_CURRENT_SOURCE_DIR}/services/joystick/vigem
        ${CMAKE_CURRENT_SOURCE_DIR}/services/joystick/vigem/sdk
        ${CMAKE_CURRENT_SOURCE_DIR}/services/joystick/vigem/sdk/ViGem)
target_compile_features(render_domain_services PUBLIC cxx_std_23)
target_link_libraries(render_domain_services
    PUBLIC render_architecture_core px_common_new px_message px_ft_engine px_voice_call
    PRIVATE Setupapi.lib)

if(TESTS_ENABLED)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_voice_call_runtime
        services/voice_call/tests/test_voice_call_runtime.cpp)
    target_include_directories(test_voice_call_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/services/voice_call)
    target_link_libraries(test_voice_call_runtime PRIVATE
        render_domain_services GTest::gtest GTest::gtest_main)
    add_test(NAME render_voice_call_runtime COMMAND test_voice_call_runtime)
    set_tests_properties(render_voice_call_runtime PROPERTIES
        LABELS "render-lifecycle;render-integration")
    if(WIN32)
        add_custom_command(TARGET test_voice_call_runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:px_voice_apm>
                $<TARGET_FILE_DIR:test_voice_call_runtime>)
    endif()

    add_executable(test_voice_call_service
        ../tests/unit/test_voice_call_service.cpp)
    target_link_libraries(test_voice_call_service PRIVATE
        render_domain_services GTest::gtest GTest::gtest_main)
    add_test(NAME render_voice_call_service COMMAND test_voice_call_service)
    set_tests_properties(render_voice_call_service PROPERTIES
        LABELS "render-unit")
    if(WIN32)
        add_custom_command(TARGET test_voice_call_service POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:px_voice_apm>
                $<TARGET_FILE_DIR:test_voice_call_service>)
    endif()
endif()
