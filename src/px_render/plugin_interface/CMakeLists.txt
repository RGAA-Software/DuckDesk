project(px_plugin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/px_plugin_interface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_stream_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_video_encoder_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_plugin_context.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_monitor_capture_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_frame_processor_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_data_provider_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_audio_encoder_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/px_frame_carrier_plugin.cpp
)

add_library(${PROJECT_NAME} ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new)

set(NET_PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/px_net_plugin.cpp
)
add_library(px_net_plugin ${NET_PLUGIN_SRCS})
target_link_libraries(px_net_plugin PRIVATE px_plugin protobuf::libprotobuf px_message px_common_new)
