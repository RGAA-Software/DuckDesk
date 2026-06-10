project(plugin_ssl_proxy)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ssl_proxy_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ssl_proxy_server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ws_local_client.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/http_handler.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE tc_common_new tc_net_plugin tc_message tc_plugin)
