message(STATUS "file transmission(server) is enabled.")
project(file_transfer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/file_transfer_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/file_transmission_server/file_operate.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/file_transmission_server/file_transmit_impl.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/file_transmission_server/file_transmit_manager.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/file_transmission_server/file_transmit_msg_interface.cc
)

add_compile_options("/bigobj")
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:83886080")
endif()

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_net_plugin
    protobuf::libprotobuf px_message Winmm.lib)
