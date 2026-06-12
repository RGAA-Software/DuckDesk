cmake_minimum_required(VERSION 3.22)
project(tc_server_protocol)

set(CMAKE_CXX_STANDARD 20)

set(PROTO_LIB_NAMES protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)

if(ANDROID)
    find_package(Protobuf REQUIRED)
    message("==> Protobuf libraries: ${Protobuf_LIBRARIES}")
    ### relay message
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS relay_message.proto)
    add_library(tc_relay_message
        ${CMAKE_CURRENT_BINARY_DIR}/relay_message.pb.cc
    )
    target_link_libraries(tc_relay_message ${PROTO_LIB_NAMES})

    ### server
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS spvr_client.proto spvr_panel.proto)
    add_library(tc_server_message
        ${CMAKE_CURRENT_BINARY_DIR}/spvr_client.pb.cc
        ${CMAKE_CURRENT_BINARY_DIR}/spvr_panel.pb.cc
    )
    target_link_libraries(tc_server_message ${PROTO_LIB_NAMES})

elseif(WIN32 OR APPLE OR UNIX)

    ### relay message
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS relay_message.proto)
    add_library(tc_relay_message
        ${CMAKE_CURRENT_BINARY_DIR}/relay_message.pb.cc
    )
    target_link_libraries(tc_relay_message ${PROTO_LIB_NAMES})

    ### server
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS spvr_client.proto spvr_panel.proto)
    add_library(tc_server_message
        ${CMAKE_CURRENT_BINARY_DIR}/spvr_client.pb.cc
        ${CMAKE_CURRENT_BINARY_DIR}/spvr_panel.pb.cc
    )
    target_link_libraries(tc_server_message ${PROTO_LIB_NAMES})
endif()