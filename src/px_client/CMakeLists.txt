project(px_client)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icon.ico ${CMAKE_CURRENT_BINARY_DIR}/icon.ico COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icon.rc.in ${CMAKE_CURRENT_BINARY_DIR}/icon.rc @ONLY)

add_subdirectory(anti_hooking)
include(rdp/rdp_freerdp_sdk.cmake)

add_library(px_client_imgui_runtime STATIC
    ui_imgui/client_audio_output.cpp
    ui_imgui/client_file_transfer_format.cpp
    ui_imgui/client_file_transfer_panel.cpp
    ui_imgui/client_file_transfer_window.cpp
    ui_imgui/client_file_browser_model.cpp
    ui_imgui/client_input_mapper.cpp
    ui_imgui/client_instance_guard.cpp
    ui_imgui/client_launch_config.cpp
    ui_imgui/client_local_file_system.cpp
    ui_imgui/client_session.cpp
    ui_imgui/client_session_display.cpp
    ui_imgui/client_session_features.cpp
    ui_imgui/client_session_input.cpp
    ui_imgui/client_session_lifecycle.cpp
    ui_imgui/client_session_rdp.cpp
    ui_imgui/client_startup_dialog.cpp
    ui_imgui/client_text.cpp
    ui_imgui/client_video_frame.cpp
    ui_imgui/client_window.cpp
    ui_imgui/client_toolbar.cpp
    rdp/rdp_frame.cpp
    rdp/rdp_session.cpp
    rdp/rdp_text_clipboard_channel.cpp
    ct_virtual_display_protocol.cpp
)
target_include_directories(px_client_imgui_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
target_link_libraries(px_client_imgui_runtime PUBLIC
    px_desktop_shell px_ui px_common px_message px_ft_engine px_voice_call px_rdp_stream pixels::sdk SDL3::SDL3-static yuv
    nlohmann_json::nlohmann_json pixels_rdp_sdk
)

add_executable(px_client WIN32 ui_imgui/client_main.cpp ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
target_link_libraries(px_client PRIVATE px_client_imgui_runtime)
target_link_options(px_client PRIVATE "/ENTRY:mainCRTStartup")
set_target_properties(px_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}")
pixels_stage_desktop_images(px_client)

add_custom_command(TARGET px_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_client>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_client>/resources/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_ui/resources/fonts/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_client>/resources/fonts/Roboto-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_deps/px_imgui/misc/fonts/Roboto-Medium.ttf"
        "$<TARGET_FILE_DIR:px_client>/resources/fonts/Roboto-Medium.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/web/px_web_client/src/assets/px_icon.png"
        "$<TARGET_FILE_DIR:px_client>/resources/icons/px_icon.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_deps/px_3rdparty/libplacebo/lib/libplacebo-349.dll"
        "$<TARGET_FILE_DIR:px_client>/libplacebo-349.dll"
)
pixels_stage_rdp_client(px_client)

if(BUILD_TESTING AND TARGET GTest::gtest)
    add_executable(test_client_imgui_launch_config tests/test_imgui_launch_config.cpp ui_imgui/client_launch_config.cpp)
    target_include_directories(test_client_imgui_launch_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
    target_link_libraries(test_client_imgui_launch_config PRIVATE px_common nlohmann_json::nlohmann_json GTest::gtest GTest::gtest_main)
    add_test(NAME client_imgui_launch_config COMMAND test_client_imgui_launch_config)

    add_executable(test_client_input_mapper tests/test_client_input_mapper.cpp ui_imgui/client_input_mapper.cpp)
    target_include_directories(test_client_input_mapper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
    target_link_libraries(test_client_input_mapper PRIVATE SDL3::SDL3-static GTest::gtest GTest::gtest_main)
    add_test(NAME client_input_mapper COMMAND test_client_input_mapper)

    add_executable(test_client_instance_guard tests/test_client_instance_guard.cpp ui_imgui/client_instance_guard.cpp)
    target_include_directories(test_client_instance_guard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
    target_link_libraries(test_client_instance_guard PRIVATE px_common GTest::gtest GTest::gtest_main)
    add_test(NAME client_instance_guard COMMAND test_client_instance_guard)

    add_executable(test_client_local_file_system tests/test_client_local_file_system.cpp ui_imgui/client_local_file_system.cpp)
    target_include_directories(test_client_local_file_system PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
    target_link_libraries(test_client_local_file_system PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME client_local_file_system COMMAND test_client_local_file_system)

    add_executable(test_client_file_browser_model tests/test_client_file_browser_model.cpp ui_imgui/client_file_browser_model.cpp)
    target_include_directories(test_client_file_browser_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
    target_link_libraries(test_client_file_browser_model PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME client_file_browser_model COMMAND test_client_file_browser_model)

    add_executable(test_client_file_transfer_format tests/test_client_file_transfer_format.cpp ui_imgui/client_file_transfer_format.cpp)
    target_include_directories(test_client_file_transfer_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
    target_link_libraries(test_client_file_transfer_format PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME client_file_transfer_format COMMAND test_client_file_transfer_format)
endif()

if(PX_PROTECTION_ENABLED)
    target_compile_definitions(px_client PRIVATE PX_PROTECTION_ENABLED=1)
    target_link_libraries(px_client PRIVATE px_protection)
endif()
