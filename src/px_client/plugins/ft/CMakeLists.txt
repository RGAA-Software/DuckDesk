project(ft_client)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ft_client_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ft_core.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_file_panel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_transfer_queue.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_overwrite_dialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ft_res.qrc
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE
        px_common_new px_message px_client_plugin px_ft_engine px_qt_widget
        Qt6::Core Qt6::Widgets Qt6::Svg Dwmapi.lib)
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/px_client/plugin_interface
        ${CMAKE_SOURCE_DIR}/src/px_client
        ${CMAKE_SOURCE_DIR}/src/px_deps/px_qt_widget
)
