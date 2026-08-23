project(px_console_client)

set(SRCS
    console_api.cpp
    console_device.cpp
    console_device_api.cpp
    console_user.cpp
    console_user_api.cpp
    console_stream.cpp
    console_user_device.cpp
    console_user_device_api.cpp
    console_user_app_api.cpp
    console_event.cpp
    console_event_api.cpp
    console_http_client.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
if(ANDROID)
#    find_package(Protobuf REQUIRED)
#    target_link_libraries(${PROJECT_NAME} px_relay_message protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)
else ()
#    target_link_libraries(${PROJECT_NAME} px_relay_message libprotobuf)
endif ()
