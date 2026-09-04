project(net_udp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TRANSPORT_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/udp_transport.cpp
)

add_library(${PROJECT_NAME} STATIC ${TRANSPORT_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_plugin)
