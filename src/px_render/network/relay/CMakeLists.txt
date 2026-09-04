project(net_relay)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TRANSPORT_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/relay_transport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relay_transport_runtime.cpp
)

add_library(${PROJECT_NAME} STATIC ${TRANSPORT_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_plugin px_relay_client px_message px_relay_message protobuf::libprotobuf)
