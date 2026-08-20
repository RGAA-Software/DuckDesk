project(ft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ft_plugin.cpp
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SRCS})
# See px_ft_engine: keep the plugin translation unit in lock-step with the
# generated protobuf ABI even when Ninja cannot parse localized MSVC includes.
set_property(SOURCE ft_plugin.cpp APPEND PROPERTY OBJECT_DEPENDS
        "${PX_PROJECT_BINARY_PATH}/px_message_new/px_message.pb.h;${PX_PROJECT_BINARY_PATH}/px_message_new/px_file_transfer.pb.h")
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin px_ft_engine)
