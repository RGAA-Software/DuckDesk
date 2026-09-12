project(px_client)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(${CMAKE_SOURCE_DIR}/src/px_panel/icon.ico ${CMAKE_CURRENT_BINARY_DIR}/icon.ico COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icon.rc ${CMAKE_CURRENT_BINARY_DIR}/icon.rc COPYONLY)

add_subdirectory(anti_hooking)
include(rdp/rdp_freerdp_sdk.cmake)

add_library(px_client_imgui_runtime STATIC
    ui_imgui/client_audio_output.cpp
    ui_imgui/client_file_transfer_panel.cpp
    ui_imgui/client_launch_config.cpp
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
    nlohmann_json::nlohmann_json gammaray_rdp_sdk
)
set_target_properties(px_client_imgui_runtime PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(px_client WIN32 ui_imgui/client_main.cpp ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
target_link_libraries(px_client PRIVATE px_client_imgui_runtime)
target_link_options(px_client PRIVATE "/ENTRY:mainCRTStartup")
set_target_properties(px_client PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF RUNTIME_OUTPUT_DIRECTORY "${PX_PROJECT_BINARY_PATH}")

add_custom_command(TARGET px_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:px_client>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/px_panel/resources/qt_material/fonts/roboto/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:px_client>/resources/fonts/Roboto-Regular.ttf"
)
gammaray_stage_rdp_client(px_client)

if(BUILD_TESTING AND TARGET GTest::gtest)
    add_executable(test_client_imgui_launch_config tests/test_imgui_launch_config.cpp ui_imgui/client_launch_config.cpp)
    target_include_directories(test_client_imgui_launch_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_imgui)
    target_link_libraries(test_client_imgui_launch_config PRIVATE px_common nlohmann_json::nlohmann_json GTest::gtest GTest::gtest_main)
    set_target_properties(test_client_imgui_launch_config PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME client_imgui_launch_config COMMAND test_client_imgui_launch_config)
endif()

if(PX_PROTECTION_ENABLED)
    target_compile_definitions(px_client PRIVATE PX_PROTECTION_ENABLED=1)
    target_link_libraries(px_client PRIVATE px_protection)
endif()
