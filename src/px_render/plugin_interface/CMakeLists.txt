project(px_plugin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMPATIBILITY_ABI_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/px_plugin_interface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_plugin_context.cpp
)

add_library(${PROJECT_NAME} ${COMPATIBILITY_ABI_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new)

set(NETWORK_COMPATIBILITY_ABI_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/px_net_plugin.cpp
)
add_library(px_net_plugin ${NETWORK_COMPATIBILITY_ABI_SRCS})
target_link_libraries(px_net_plugin PRIVATE px_plugin protobuf::libprotobuf px_message px_common_new)
