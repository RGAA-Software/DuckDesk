project(client_clipboard_module)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODULE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/clipboard_module.cpp
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

add_library(${PROJECT_NAME} STATIC ${MODULE_SRCS} ${CLIPBOARD_WIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_client_module_runtime px_common_new px_message Winmm.lib Wtsapi32.lib Qt6::Core Qt6::Widgets)
target_include_directories(${PROJECT_NAME} PRIVATE
    
    ${CMAKE_SOURCE_DIR}/src/px_client
)
