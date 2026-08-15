project(plugin_net_relay)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/relay_plugin.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_net_plugin px_relay_client px_message px_relay_message protobuf::libprotobuf)
target_link_libraries(${PROJECT_NAME} PRIVATE)
