project(media_record_client)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/media_record_plugin.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/media_recorder.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_client_plugin Qt6::Core Qt6::Widgets)
# remux 逻辑在共享库 px_media_record_new(内部链接 ffmpeg)
target_link_libraries(${PROJECT_NAME} PRIVATE px_media_record_new)
target_include_directories(${PROJECT_NAME} PRIVATE

    ${CMAKE_SOURCE_DIR}/src/px_client/plugin_interface
    ${CMAKE_SOURCE_DIR}/src/px_client
)
