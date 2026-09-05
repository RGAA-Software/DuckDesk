project(px_client)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network Charts Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network Charts Svg)

add_library(px_client_module_runtime STATIC
        modules/client_module_context.cpp
)
target_link_libraries(px_client_module_runtime PRIVATE
        Qt6::Core px_common)
target_include_directories(px_client_module_runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# px_panel/icon.ico is the canonical Windows executable icon for both apps.
configure_file(${CMAKE_SOURCE_DIR}/src/px_panel/icon.ico ${CMAKE_CURRENT_BINARY_DIR}/icon.ico COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icon.rc ${CMAKE_CURRENT_BINARY_DIR}/icon.rc COPYONLY)

message("Client => CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

find_package(Qt6 REQUIRED COMPONENTS Widgets Core OpenGL OpenGLWidgets Svg Charts)
find_package(OpenSSL REQUIRED)

include_directories(${VK_SDK_ROOT}/Include)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/px_panel/src)
include_directories(${CMAKE_BINARY_DIR}/src/px_base)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

link_directories(${VK_SDK_ROOT}/Lib)
link_directories(${PX_PROJECT_PATH}/px_3rdparty/libplacebo/lib)

qt_add_resources(RES ${CMAKE_SOURCE_DIR}/src/px_panel/resources/main.qrc resources.qrc)
add_compile_options(/bigobj)

set(SRCS_WS
    ct_main_ws.cpp
    ct_client_context.cpp
    ct_workspace.cpp
    ct_base_workspace.cpp
    ct_virtual_display_protocol.cpp
    ct_voice_call_protocol.cpp
    front_render/ct_video_widget.cpp
    front_render/opengl/ct_director.cpp
    front_render/opengl/ct_opengl_video_widget.cpp
    front_render/opengl/ct_shader_program.cpp
    front_render/opengl/ct_opengl_image_reader.cpp
    front_render/opengl/ct_renderer.cpp
    front_render/opengl/ct_sprite.cpp
    front_render/sdl/ct_sdl_video_widget.cpp
    front_render/vulkan/ct_vulkan_video_widget.cpp
    front_render/vulkan/pl_vulkan.cpp
    front_render/vulkan/plvk_c.c
    front_render/vulkan/ct_test_vulkan_video_widget.cpp
    front_render/vulkan/ct_test_ffmpeg_vulkan_decoder.cpp
    front_render/vulkan/ct_vulkan_checker.cpp
    front_render/resource/yuv_util.cpp
    ct_qt_key_converter.cpp
    ct_audio_player.cpp
    ct_main_progress.cpp
    px_render_view.cpp
    ct_game_overlay.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/icon.rc ${RES}
    ui/switch_button.cpp
    ui/float_icon.cpp
    ui/round_rect_widget.cpp
    ui/app_menu.cpp
    ui/app_color_theme.cpp
    ui/app_content.cpp
    ui/background_widget.cpp
    ct_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/px_panel/src/theme/QtAdvancedStylesheet.cpp
    ui/float_controller.cpp
    ui/float_overlay_window.cpp
    ui/base_widget.cpp
    ui/float_controller_panel.cpp
    ui/virtual_display_ui_state.cpp
    ui/float_notification_handle.cpp
    ui/notification_panel.cpp
    ui/notification_item.cpp
    ui/key_state_panel.cpp
    ui/ct_statistics_panel.cpp
    ui/ct_stat_chart.cpp
    ui/ct_stat_frame_info_item.cpp
    ui/float_sub_control_panel.cpp
    ui/float_sub_mode_panel.cpp
    ui/float_sub_fps_panel.cpp
    ui/float_sub_display_panel.cpp
    ui/float_3rd_scale_panel.cpp
    ui/float_3rd_resolution_panel.cpp
    ui/float_button_state_indicator.cpp
    ui/computer_icon.cpp
    ui/svg_lable.cpp
    ui/media_record_sign_lab.cpp
    ui/retry_conn_dialog.cpp
    network/ct_console_client.cpp
    network/ct_auth_token.cpp
    network/ct_panel_client.cpp
    modules/client_module_manager.cpp
    api/ct_panel_api.cpp
    api/ct_render_api.cpp
)

if (WIN32)
    list(APPEND SRCS_WIN
        front_render/d3d11/ct_d3d11_video_widget.cpp
        front_render/d3d11/d3d11_render_manager.cpp
        front_render/d3d11/raw_sdl_widget.cpp
    )
endif ()

add_subdirectory(anti_hooking)
add_subdirectory(modules/clipboard)
add_subdirectory(modules/file_transfer)
add_subdirectory(modules/media_recording)
add_executable(px_client WIN32 ${SRCS_WS} ${SRCS_WIN})

if (APPLE)
    set_target_properties(px_client PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER com.px.client
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
            MACOSX_BUNDLE TRUE
            WIN32_EXECUTABLE TRUE
    )
endif()

target_link_libraries(px_client PRIVATE Qt6::Widgets Qt6::Core Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg Qt6::Charts
        px_common px_message px_cp_message px_sdk px_qt_widget OpenSSL::SSL OpenSSL::Crypto
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static> Winmm.lib px_server_message
        Dwmapi.lib unofficial::sqlite3::sqlite3 libplacebo vulkan-1 px_hw_info skin_interface skin_loader
        client_clipboard_module client_file_transfer_module client_media_recording_module
        px_voice_call
        px_rtc_client
)

add_executable(test_client_virtual_display
    tests/test_virtual_display_client.cpp
    ct_virtual_display_protocol.cpp
    ui/virtual_display_ui_state.cpp
)
target_include_directories(test_client_virtual_display PRIVATE ${CMAKE_SOURCE_DIR}/src/px_deps)
target_link_libraries(test_client_virtual_display PRIVATE px_message GTest::gtest GTest::gtest_main)

add_executable(test_client_voice_call_protocol
    tests/test_voice_call_protocol.cpp
    ct_voice_call_protocol.cpp
)
target_link_libraries(test_client_voice_call_protocol PRIVATE
    px_message px_voice_call GTest::gtest GTest::gtest_main)

add_executable(test_client_module_context_lifecycle
    tests/test_client_module_context_lifecycle.cpp
)
target_link_libraries(test_client_module_context_lifecycle PRIVATE
    px_client_module_runtime Qt6::Core GTest::gtest)

add_executable(test_client_recording_module_lifecycle
    tests/test_client_recording_module_lifecycle.cpp
)
target_link_libraries(test_client_recording_module_lifecycle PRIVATE
    client_media_recording_module px_common px_message
    Qt6::Core GTest::gtest)

add_executable(test_client_file_transfer_module_lifecycle
    tests/test_client_file_transfer_module_lifecycle.cpp
)
target_link_libraries(test_client_file_transfer_module_lifecycle PRIVATE
    client_file_transfer_module px_common px_message
    Qt6::Core Qt6::Widgets GTest::gtest)

add_executable(test_client_clipboard_module_lifecycle
    tests/test_client_clipboard_module_lifecycle.cpp
)
target_link_libraries(test_client_clipboard_module_lifecycle PRIVATE
    client_clipboard_module px_common px_message
    Qt6::Core Qt6::Widgets GTest::gtest)

add_executable(test_client_clipboard_file_stream
    tests/test_client_clipboard_file_stream.cpp
    modules/clipboard/win/cp_file_stream.cpp
)
target_link_libraries(test_client_clipboard_file_stream PRIVATE
    px_common px_message GTest::gtest GTest::gtest_main)

if(BUILD_TESTING)
    add_executable(test_client_latest_frame_dispatch_queue
        tests/test_latest_frame_dispatch_queue.cpp
    )
    target_include_directories(test_client_latest_frame_dispatch_queue PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_client_latest_frame_dispatch_queue PRIVATE GTest::gtest GTest::gtest_main)

    add_test(NAME client_voice_call_protocol COMMAND test_client_voice_call_protocol)
    add_test(NAME client_virtual_display COMMAND test_client_virtual_display)
    add_test(NAME client_latest_frame_dispatch_queue COMMAND test_client_latest_frame_dispatch_queue)
    add_test(NAME client_module_context_lifecycle COMMAND test_client_module_context_lifecycle)
    add_test(NAME client_recording_module_lifecycle COMMAND test_client_recording_module_lifecycle)
    add_test(NAME client_file_transfer_module_lifecycle COMMAND test_client_file_transfer_module_lifecycle)
    add_test(NAME client_clipboard_module_lifecycle COMMAND test_client_clipboard_module_lifecycle)
    add_test(NAME client_clipboard_file_stream COMMAND test_client_clipboard_file_stream)
endif()

if (PX_PROTECTION_ENABLED)
    add_definitions(-DPX_PROTECTION_ENABLED=1)
    target_link_libraries(px_client PRIVATE px_protection)
endif ()

if (WIN32)
    add_custom_command(TARGET px_client POST_BUILD
            COMMAND ${QT_ROOT}/bin/windeployqt.exe $<TARGET_FILE:px_client> --no-translations --release
            # ffmpeg is now statically linked via vcpkg; no DLLs to copy
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avcodec-61.dll"  "${CMAKE_BINARY_DIR}"
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avdevice-61.dll"  "${CMAKE_BINARY_DIR}"
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avfilter-10.dll"  "${CMAKE_BINARY_DIR}"
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avformat-61.dll"  "${CMAKE_BINARY_DIR}"
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avutil-59.dll"  "${CMAKE_BINARY_DIR}"
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/postproc-58.dll"  "${CMAKE_BINARY_DIR}"
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swresample-5.dll"  "${CMAKE_BINARY_DIR}"
            # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swscale-8.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${PX_PROJECT_PATH}/px_3rdparty/libplacebo/lib/libplacebo-349.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/px_panel/resources/language" "${CMAKE_CURRENT_BINARY_DIR}/resources/language"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/px_panel/resources/qt_material" "${CMAKE_CURRENT_BINARY_DIR}/resources/qt_material"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/deps/theme"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${PX_PROJECT_BINARY_PATH}/px_skins" "${CMAKE_CURRENT_BINARY_DIR}/deps/theme"
    )

elseif (APPLE)
elseif (UNIX)

endif ()
