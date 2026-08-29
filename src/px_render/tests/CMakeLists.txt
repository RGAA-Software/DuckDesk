find_package(GTest CONFIG REQUIRED)

add_executable(test_render_service_rpc_state test_render_service_rpc_state.cpp)
target_link_libraries(test_render_service_rpc_state
    PRIVATE px_common_new GTest::gtest GTest::gtest_main)

add_executable(test_ft_plugin_dll_lifecycle test_ft_plugin_dll_lifecycle.cpp)
target_link_libraries(test_ft_plugin_dll_lifecycle
    PRIVATE px_common_new px_message px_plugin GTest::gtest GTest::gtest_main)
target_compile_definitions(test_ft_plugin_dll_lifecycle
    PRIVATE PX_FT_PLUGIN_DLL_PATH="$<TARGET_FILE:ft>")
add_dependencies(test_ft_plugin_dll_lifecycle ft)

add_executable(test_plugin_context_lifecycle test_plugin_context_lifecycle.cpp)
target_link_libraries(test_plugin_context_lifecycle
    PRIVATE px_common_new px_plugin GTest::gtest GTest::gtest_main)

add_executable(test_relay_plugin_dll_lifecycle test_relay_plugin_dll_lifecycle.cpp)
target_link_libraries(test_relay_plugin_dll_lifecycle
    PRIVATE px_common_new px_message px_plugin GTest::gtest GTest::gtest_main)
target_compile_definitions(test_relay_plugin_dll_lifecycle
    PRIVATE PX_RELAY_PLUGIN_DLL_PATH="$<TARGET_FILE:net_relay>")
add_dependencies(test_relay_plugin_dll_lifecycle net_relay)

add_executable(test_was_audio_plugin_dll_lifecycle
        test_was_audio_plugin_dll_lifecycle.cpp)
target_link_libraries(test_was_audio_plugin_dll_lifecycle
        PRIVATE GTest::gtest_main px_common_new px_plugin)
target_compile_definitions(test_was_audio_plugin_dll_lifecycle
        PRIVATE PX_WAS_AUDIO_PLUGIN_DLL_PATH="$<TARGET_FILE:cap_was_audio>")
add_dependencies(test_was_audio_plugin_dll_lifecycle cap_was_audio)

add_executable(test_media_recorder_plugin_dll_lifecycle
        test_media_recorder_plugin_dll_lifecycle.cpp)
target_link_libraries(test_media_recorder_plugin_dll_lifecycle
        PRIVATE GTest::gtest_main px_common_new px_plugin)
target_compile_definitions(test_media_recorder_plugin_dll_lifecycle
        PRIVATE PX_MEDIA_RECORDER_PLUGIN_DLL_PATH="$<TARGET_FILE:media_recorder>")
add_dependencies(test_media_recorder_plugin_dll_lifecycle media_recorder)

add_executable(test_live_pusher_plugin_dll_lifecycle
        test_live_pusher_plugin_dll_lifecycle.cpp)
target_link_libraries(test_live_pusher_plugin_dll_lifecycle
        PRIVATE GTest::gtest_main px_common_new px_plugin)
target_compile_definitions(test_live_pusher_plugin_dll_lifecycle
        PRIVATE PX_LIVE_PUSHER_PLUGIN_DLL_PATH="$<TARGET_FILE:live_pusher>")
add_dependencies(test_live_pusher_plugin_dll_lifecycle live_pusher)
