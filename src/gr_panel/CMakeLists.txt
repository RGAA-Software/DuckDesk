set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${GR_PROJECT_BINARY_PATH})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${GR_PROJECT_BINARY_PATH})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${GR_PROJECT_BINARY_PATH})

# configure_file for version_config.h and icon.rc are in src/GammaRay/CMakeLists.txt
# icon.rc is generated at ${CMAKE_BINARY_DIR}/src/GammaRay/icon.rc

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/ui/gd_gui/src)
include_directories(${GR_PROJECT_PATH}/deps)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/eventbus/src)
include_directories(${GR_PROJECT_BINARY_PATH})
include_directories(${GR_PROJECT_BINARY_PATH}/deps/tc_message_new)
include_directories(${GR_PROJECT_BINARY_PATH}/deps/tc_server_protocol)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/asio2/include)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/asio2/3rd)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/asio2/3rd/asio)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/sqlite35)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/sqlite_orm/include)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/webrtc/include/third_party/libyuv/include)
include_directories(${GR_PROJECT_PATH}/deps/tc_3rdparty/leveldb/include)
include_directories(${CMAKE_SOURCE_DIR}/src/gr_render)
include_directories(${GR_PROJECT_PATH}/deps/tc_qt_widget)
include_directories(${GR_PROJECT_PATH}/deps/tc_client_sdk_new)
include_directories(${CMAKE_BINARY_DIR}/src/GammaRay)

add_subdirectory(src)
add_subdirectory(src/tests)
add_subdirectory(src/service)
add_subdirectory(src/uninstall)
add_subdirectory(src/ui/gd_gui)

set(RES resources/main.qrc resources/qss.qrc res.qrc)
add_executable(GammaRay WIN32
        main.cpp
        ${RES}
        ${MAIN_SRCS}
        ${NETWORK_SRCS}
        ${CMAKE_BINARY_DIR}/src/GammaRay/icon.rc
        ${SERVICE_MGR_SRCS}
        ${DEVICE_LIST_SRCS}
)

SET_TARGET_PROPERTIES(GammaRay PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\" /SUBSYSTEM:WINDOWS")

target_link_libraries(GammaRay PRIVATE Qt6::Widgets Qt6::Network Qt6::Core Qt6::Core5Compat Qt6::WebSockets Qt6::Network Qt6::Charts Qt6::Svg sqlite35
        tc_common_new tc_steam_manager_new tc_vigem tc_message tc_cp_message tc_plugin tc_qt_widget winmm.lib SetupAPI.lib gflags::gflags
        tc_relay_client tc_translator tc_spvr_client Dwmapi.lib tc_widget_loadings tc_client_plugin
        tc_account_sdk tc_rp_message tc_profile_client tc_hw_info tc_global_id_generator tc_server_message skin_interface skin_loader gd_gui
)
set(BREAKPAD_LIB_PATH ${VCPKG_ROOT}/packages/breakpad_x64-windows-static/lib)
target_link_libraries(GammaRay PRIVATE ${BREAKPAD_LIB_PATH}/libbreakpad.lib ${BREAKPAD_LIB_PATH}/libbreakpad_client.lib)

add_custom_command(TARGET GammaRay PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/gr_plugins
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gr_plugins
)

### Uninstall
add_executable(uninstall WIN32
        ${CMAKE_BINARY_DIR}/src/GammaRay/icon.rc
        ${UNINSTALL_SRCS}
        ${SERVICE_MGR_SRCS})

SET_TARGET_PROPERTIES(uninstall PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
add_executable(panel_shadow_deleter ${SHADOW_DELETER_SRCS})
SET_TARGET_PROPERTIES(panel_shadow_deleter PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

target_link_libraries(uninstall PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network tc_qt_widget
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Core5Compat Qt6::WebSockets Qt6::Network Qt6::Charts Qt6::Svg)
target_link_libraries(uninstall PRIVATE tc_common_new tc_steam_manager_new tc_vigem tc_message sqlite35 tc_plugin winmm.lib SetupAPI.lib gflags::gflags)
### Uninstall End

include(GNUInstallDirs)
install(TARGETS GammaRay
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_custom_command(TARGET GammaRay POST_BUILD
        COMMAND ${QT_ROOT}/bin/windeployqt.exe $<TARGET_FILE:GammaRay> --release
        COMMAND ${CMAKE_COMMAND} -E copy "${GR_PROJECT_PATH}/deps/tc_controller/vigem/driver/joystick.exe"  "${CMAKE_CURRENT_BINARY_DIR}/"
)
