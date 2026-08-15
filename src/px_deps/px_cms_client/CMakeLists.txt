project(px_cms_client)

set(SRCS
    cms_api.cpp
    cms_device.cpp
    cms_device_api.cpp
    cms_user.cpp
    cms_user_api.cpp
    cms_stream.cpp
    cms_user_device.cpp
    cms_user_device_api.cpp
    cms_event.cpp
    cms_event_api.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
if(ANDROID)
#    find_package(Protobuf REQUIRED)
#    target_link_libraries(${PROJECT_NAME} px_relay_message protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)
else ()
#    target_link_libraries(${PROJECT_NAME} px_relay_message libprotobuf)
endif ()