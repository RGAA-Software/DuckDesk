project(GammaRayRender VERSION ${TC_APP_VERSION} LANGUAGES CXX)

add_subdirectory(settings)
add_subdirectory(plugin_interface)
add_subdirectory(plugins)
add_subdirectory(hook_capture)

#find_package(OpenCV CONFIG REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/asio2/include)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/asio2/3rd)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/asio2/3rd/asio)

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/rd_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/rd_icon.rc"
        @ONLY
)

set(SRCS
        rd_app.cpp
        rd_context.cpp
        rd_statistics.cpp
        network/ws_media_router.cpp
        network/ws_ipc_router.cpp
        network/ws_panel_client.cpp
        network/net_message_maker.cpp
        network/server_cast.cpp
        network/render_service_client.cpp
        app/encoder_thread.cpp
        plugins/plugin_manager.cpp
        plugins/plugin_event_router.cpp
        plugins/plugin_stream_event_router.cpp
        plugins/plugin_net_event_router.cpp
        #plugins/plugin_desktop_capture.cpp
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/app)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/webrtc)

add_library(main ${SRCS})

if(WIN32)
    target_sources(main PRIVATE
            ${WIN32_SRCS}
    )
else()
    #
endif()

target_link_libraries(main app_manager libprotobuf tc_message Dexode::EventBus)

add_executable(${PROJECT_NAME} rd_main.cpp ${CMAKE_CURRENT_BINARY_DIR}/rd_icon.rc)
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level=\'requireAdministrator\' uiAccess=\'false\'\"")
target_link_libraries(${PROJECT_NAME} settings main tc_common_new tc_message tc_steam_manager_new tc_vigem tc_plugin) # tc_rtc tc_encoder_new legacy_encoder
target_link_libraries(${PROJECT_NAME} sqlite35 EasyHook64 gflags::gflags leveldb tc_rp_message)
#target_link_libraries(${PROJECT_NAME} opencv_ml opencv_dnn opencv_core opencv_flann)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/settings.toml"  "${CMAKE_BINARY_DIR}"
        #COMMAND ${QT_ROOT}/bin/windeployqt.exe $<TARGET_FILE:${PROJECT_NAME}> --release
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avcodec-61.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avdevice-61.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avfilter-10.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avformat-61.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avutil-59.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/postproc-58.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swresample-5.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
        COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swscale-8.dll"  "${CMAKE_BINARY_DIR}/src/GammaRay"
)

