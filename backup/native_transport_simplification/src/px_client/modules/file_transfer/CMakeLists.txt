project(client_file_transfer_module)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(MODULE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/file_transfer_module.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ft_core.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_file_panel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_transfer_queue.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ft_overwrite_dialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ft_res.qrc
)

add_library(${PROJECT_NAME} STATIC ${MODULE_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE
        px_common px_message px_ft_engine px_qt_widget
        Qt6::Core Qt6::Widgets Qt6::Svg Dwmapi.lib)
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/px_client
        ${CMAKE_SOURCE_DIR}/src/px_deps/px_qt_widget
)
