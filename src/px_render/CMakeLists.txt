project(px_render VERSION ${TC_APP_VERSION} LANGUAGES CXX)

add_subdirectory(settings)
add_subdirectory(plugin_interface)
add_subdirectory(plugins)
add_subdirectory(hook_capture)
if(TESTS_ENABLED)
    add_subdirectory(tests)
endif()

#find_package(OpenCV CONFIG REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/include)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd/asio)

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/rd_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/rd_icon.rc"
        @ONLY
)

set(SRCS
        rd_app.cpp
        rd_context.cpp
        rd_statistics.cpp
        session/logical_session_registry.cpp
        network/ws_media_router.cpp
        network/ws_panel_client.cpp
        network/net_message_maker.cpp
        network/server_cast.cpp
        network/render_service_client.cpp
        app/encoder_thread.cpp
        plugins/plugin_manager.cpp
        plugins/plugin_event_router.cpp
        plugins/plugin_stream_event_router.cpp
        plugins/plugin_net_event_router.cpp
        webview/webview_runtime.cpp
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

target_link_libraries(main app_manager protobuf::libprotobuf px_message)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 rd_main.cpp ${CMAKE_CURRENT_BINARY_DIR}/rd_icon.rc)
else()
    add_executable(${PROJECT_NAME} rd_main.cpp ${CMAKE_CURRENT_BINARY_DIR}/rd_icon.rc)
endif()
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level=\'requireAdministrator\' uiAccess=\'false\'\" /ENTRY:mainCRTStartup")
target_link_libraries(${PROJECT_NAME} settings main px_common_new px_message px_steam_manager_new px_vigem px_plugin) # px_rtc px_encoder_new legacy_encoder
if(WIN32)
    add_dependencies(${PROJECT_NAME} libcef_dll_wrapper)
    target_include_directories(main PRIVATE "${CEF_ROOT}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${CEF_ROOT}")
    target_link_libraries(main libcef_dll_wrapper d3d11 dxgi)
    target_link_libraries(${PROJECT_NAME} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
endif()
# easyhook does not have a static triplet, keep using the dynamic x64-windows package
target_link_directories(${PROJECT_NAME} PRIVATE ${VCPKG_ROOT}/installed/x64-windows/lib)
target_link_libraries(${PROJECT_NAME} unofficial::sqlite3::sqlite3 EasyHook64 gflags::gflags leveldb::leveldb px_rp_message)
#target_link_libraries(${PROJECT_NAME} opencv_ml opencv_dnn opencv_core opencv_flann)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/settings.toml"  "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/settings.toml"  "${PX_PROJECT_BINARY_PATH}"
        #COMMAND ${QT_ROOT}/bin/windeployqt.exe $<TARGET_FILE:${PROJECT_NAME}> --release
        # ffmpeg is now statically linked via vcpkg; no DLLs to copy
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avcodec-61.dll"  "${PX_PROJECT_BINARY_PATH}"
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avdevice-61.dll"  "${PX_PROJECT_BINARY_PATH}"
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avfilter-10.dll"  "${PX_PROJECT_BINARY_PATH}"
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avformat-61.dll"  "${PX_PROJECT_BINARY_PATH}"
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/avutil-59.dll"  "${PX_PROJECT_BINARY_PATH}"
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/postproc-58.dll"  "${PX_PROJECT_BINARY_PATH}"
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swresample-5.dll"  "${PX_PROJECT_BINARY_PATH}"
        # COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG_ROOT}/bin/swscale-8.dll"  "${PX_PROJECT_BINARY_PATH}"
)

if(WIN32)
    # CEF subprocesses are px_render.exe itself, therefore all runtime DLLs and
    # resources must be adjacent to px_render.exe in both local and packaged builds.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(PX_CEF_RUNTIME_BINARY_DIR "${CEF_BINARY_DIR_DEBUG}")
    else()
        set(PX_CEF_RUNTIME_BINARY_DIR "${CEF_BINARY_DIR_RELEASE}")
    endif()
    COPY_FILES("${PROJECT_NAME}" "${CEF_BINARY_FILES}" "${PX_CEF_RUNTIME_BINARY_DIR}"
               "${CMAKE_CURRENT_BINARY_DIR}")
    COPY_FILES("${PROJECT_NAME}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}"
               "${CMAKE_CURRENT_BINARY_DIR}")
endif()
