project(px_relay_client)

set(SRCS
        relay_context.cpp
        relay_client_sdk.cpp
        relay_ws_client.cpp
        relay_room.cpp
        relay_server_sdk.cpp
        relay_api.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
target_link_libraries(${PROJECT_NAME} px_common_new)
if(ANDROID)
    find_package(Protobuf REQUIRED)
    target_link_libraries(${PROJECT_NAME} px_relay_message protobuf::libprotobuf protobuf::libprotobuf-lite)
else ()
    target_link_libraries(${PROJECT_NAME} px_relay_message protobuf::libprotobuf)
    target_link_libraries(${PROJECT_NAME} Qt6::Core Qt6::Core5Compat Qt6::Widgets)

    add_subdirectory(test)
endif ()
