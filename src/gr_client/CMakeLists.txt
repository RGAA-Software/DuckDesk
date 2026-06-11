project(GammaRayClient)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network Charts Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network Charts Svg)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_interface/ct_plugin_interface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_interface/ct_plugin_context.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_interface/ct_media_record_plugin_interface.cpp
)

add_library(tc_client_plugin ${PLUGIN_SRCS})
target_link_libraries(tc_client_plugin PRIVATE Qt6::Widgets Qt6::Network Qt6::Core Qt6::Core5Compat
        Qt6::WebSockets Qt6::Network Qt6::Charts Qt6::Svg tc_common_new)

target_include_directories(tc_client_plugin PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_interface
)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

message("Client => CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

find_package(Qt6 REQUIRED COMPONENTS Widgets Core OpenGL OpenGLWidgets Svg Charts)
find_package(OpenSSL REQUIRED)

include_directories(${VK_SDK_ROOT}/Include)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/gr_panel/src)
include_directories(${CMAKE_BINARY_DIR}/src/GammaRay)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/plugins/file_transfer_client/src)

link_directories(${VK_SDK_ROOT}/Lib)
link_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/libplacebo/lib)

qt_add_resources(RES ${CMAKE_SOURCE_DIR}/src/gr_panel/resources/main.qrc resources.qrc)
add_compile_options(/bigobj)

set(SRCS_WS
    ct_main_ws.cpp
    ct_client_context.cpp
    ct_workspace.cpp
    ct_base_workspace.cpp
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
    ct_game_view.cpp
    ct_game_overlay.cpp
    icon.rc ${RES}
    ui/switch_button.cpp
    ui/float_icon.cpp
    ui/round_rect_widget.cpp
    ui/app_menu.cpp
    ui/app_color_theme.cpp
    ui/app_content.cpp
    ui/background_widget.cpp
    ct_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/gr_panel/src/theme/QtAdvancedStylesheet.cpp
    ui/float_controller.cpp
    ui/base_widget.cpp
    ui/float_controller_panel.cpp
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
    network/ct_spvr_client.cpp
    network/ct_panel_client.cpp
    plugins/ct_plugin_manager.cpp
    plugins/ct_plugin_event_router.cpp
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
add_subdirectory(plugins)
add_executable(GammaRayClientInner WIN32 ${SRCS_WS} ${CLIPBOARD_SRCS} ${CLIPBOARD_WIN_SRCS} ${SRCS_WIN})
if (APPLE)
    set_target_properties(GammaRayClientInner PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER com.tc.client.client
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
            MACOSX_BUNDLE TRUE
            WIN32_EXECUTABLE TRUE
    )
endif()

target_link_libraries(GammaRayClientInner PRIVATE Qt6::Widgets Qt6::Core Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg Qt6::Charts
        tc_common_new tc_message tc_cp_message tc_sdk tc_qt_widget OpenSSL::SSL OpenSSL::Crypto
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static> Winmm.lib tc_server_message
        tc_rtc_client Dwmapi.lib tc_client_plugin sqlite35 libplacebo vulkan-1 tc_hw_info skin_interface skin_loader
)

if (TC_PROTECTION_ENABLED)
    add_definitions(-DTC_PROTECTION_ENABLED=1)
    target_link_libraries(GammaRayClientInner PRIVATE tc_protection)
endif ()

if (WIN32)
    add_custom_command(TARGET GammaRayClientInner POST_BUILD
            COMMAND ${QT_ROOT}/bin/windeployqt.exe $<TARGET_FILE:GammaRayClientInner> --no-translations --release
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avcodec-61.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avdevice-61.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avfilter-10.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avformat-61.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avutil-59.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/postproc-58.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swresample-5.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swscale-8.dll"  "${CMAKE_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${GR_PROJECT_PATH}/deps/tc_3rdparty/libplacebo/lib/libplacebo-349.dll"  "${CMAKE_BINARY_DIR}"
    )

elseif (APPLE)
elseif (UNIX)

endif ()
