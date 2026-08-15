project(tc_plugin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_plugin_interface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_stream_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_video_encoder_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_plugin_context.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_monitor_capture_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_frame_processor_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_data_provider_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_audio_encoder_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_frame_carrier_plugin.cpp
)

add_library(${PROJECT_NAME} ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new)

set(NET_PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/gr_net_plugin.cpp
)
add_library(tc_net_plugin ${NET_PLUGIN_SRCS})
target_link_libraries(tc_net_plugin PRIVATE tc_plugin protobuf::libprotobuf tc_message px_common_new)
