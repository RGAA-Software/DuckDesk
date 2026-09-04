project(net_ws)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TRANSPORT_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ws_transport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ws_server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ws_stream_router.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ws_filetransfer_router.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ws_user_proxy_router.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/direct_session_grant_store.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/http_handler.cpp
)

add_library(${PROJECT_NAME} STATIC ${TRANSPORT_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE
    render_architecture_core px_common_new px_plugin
    protobuf::libprotobuf px_message px_rp_message)
