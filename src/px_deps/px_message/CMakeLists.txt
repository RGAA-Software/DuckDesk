cmake_minimum_required(VERSION 3.22)
project(px_message)

# WebRTC ships a different Abseil version. Consumers that only classify wire
# messages use an enum-only header generated from the same authoritative schema.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/px_message.proto")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/px_message.proto" PX_MESSAGE_SCHEMA)
string(REGEX MATCH "enum MessageType[ \r\n]*\\{[^}]*\\}" PX_MESSAGE_ENUM "${PX_MESSAGE_SCHEMA}")
if(NOT PX_MESSAGE_ENUM)
    message(FATAL_ERROR "px_message.proto MessageType enum was not found")
endif()
string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*[0-9]+" PX_MESSAGE_ENUM_ITEMS "${PX_MESSAGE_ENUM}")
set(PX_MESSAGE_TYPE_ENTRIES "")
foreach(PX_MESSAGE_ENUM_ITEM IN LISTS PX_MESSAGE_ENUM_ITEMS)
    string(APPEND PX_MESSAGE_TYPE_ENTRIES "    ${PX_MESSAGE_ENUM_ITEM},\n")
endforeach()
configure_file(message_type_ids.h.in message_type_ids.h @ONLY)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/include)
include_directories(${PX_PROJECT_PATH}/px_3rdparty/asio2/3rd)

if(ANDROID)
    find_package(Protobuf REQUIRED)
    message("==> Protobuf libraries: ${Protobuf_LIBRARIES}")
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_message.proto px_signaling_message.proto px_file_transfer.proto)
    add_library(px_message
            ${CMAKE_CURRENT_BINARY_DIR}/px_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_signaling_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_file_transfer.pb.cc
            proto_message_maker.cpp
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
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS px_message.proto px_service_message.proto px_signaling_message.proto px_file_transfer.proto)
    message("Protobuf of px_message.proto: ${PROTO_SRCS}")
    add_library(px_message proto_message_maker.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/px_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_service_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_signaling_message.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/px_file_transfer.pb.cc
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
target_include_directories(px_message PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(px_message px_common_core)
