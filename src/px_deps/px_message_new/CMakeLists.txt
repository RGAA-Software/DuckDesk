cmake_minimum_required(VERSION 3.22)
project(px_message)

set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${GR_PROJECT_PATH}/px_3rdparty/asio2/include)
include_directories(${GR_PROJECT_PATH}/px_3rdparty/asio2/3rd)

if(ANDROID)
    find_package(Protobuf REQUIRED)
    message("==> Protobuf libraries: ${Protobuf_LIBRARIES}")
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_message.proto px_file_transfer.proto px_signaling_message.proto)
    add_library(px_message
            ${CMAKE_CURRENT_BINARY_DIR}/px_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_file_transfer.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_signaling_message.pb.cc
            proto_message_maker.cpp
            msg_answer_cbk.cpp
            proto_converter.cpp
            )
    target_link_libraries(px_message protobuf::libprotobuf protobuf::libprotobuf-lite)

    #### Client <==> Panel message
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_client_panel_message.proto)
    add_library(px_cp_message
            ${CMAKE_CURRENT_BINARY_DIR}/px_client_panel_message.pb.cc
    )
    target_link_libraries(px_cp_message protobuf::libprotobuf protobuf::libprotobuf-lite)

    #### Renderer <==> Panel message
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_render_panel_message.proto)
    add_library(px_rp_message
            ${CMAKE_CURRENT_BINARY_DIR}/px_render_panel_message.pb.cc
            rp_proto_converter.cpp
    )
    target_link_libraries(px_rp_message protobuf::libprotobuf protobuf::libprotobuf-lite)

elseif(WIN32 OR APPLE OR UNIX)
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_message.proto px_service_message.proto px_file_transfer.proto px_signaling_message.proto)
    message("Protobuf of px_message.proto: ${PROTO_SRCS}")
    add_library(px_message proto_message_maker.cpp msg_answer_cbk.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/px_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_service_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_file_transfer.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_signaling_message.pb.cc
            proto_converter.cpp
    )
    target_link_libraries(px_message protobuf::libprotobuf)

    #### Client <==> Panel message
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_client_panel_message.proto)
    add_library(px_cp_message
            ${CMAKE_CURRENT_BINARY_DIR}/px_client_panel_message.pb.cc
    )
    target_link_libraries(px_cp_message protobuf::libprotobuf)


    #### Renderer <==> Panel message
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_render_panel_message.proto)
    add_library(px_rp_message
            ${CMAKE_CURRENT_BINARY_DIR}/px_render_panel_message.pb.cc
            rp_proto_converter.cpp
    )
    target_link_libraries(px_rp_message protobuf::libprotobuf)

endif()