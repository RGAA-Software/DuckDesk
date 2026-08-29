project(client_clipboard)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/clipboard_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/clipboard_runtime_bridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ct_clipboard_manager.cpp
)

if(WIN32)
    set(CLIPBOARD_WIN_SRCS
            ${CMAKE_CURRENT_SOURCE_DIR}/win/cp_data_object.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/win/cp_file_stream.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/win/cp_virtual_file.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/win/win_message_loop.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/win/win_message_window.cpp
    )
else ()
    set(CLIPBOARD_WIN_SRCS)
endif ()

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS} ${CLIPBOARD_WIN_SRCS})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME clipboard)
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_client_plugin Winmm.lib Wtsapi32.lib Qt6::Core Qt6::Widgets)
target_include_directories(${PROJECT_NAME} PRIVATE
    
    ${CMAKE_SOURCE_DIR}/src/px_client/plugin_interface
    ${CMAKE_SOURCE_DIR}/src/px_client
)
