project(tc_spvr_client)

set(SRCS
    spvr_api.cpp
    spvr_device.cpp
    spvr_device_api.cpp
    spvr_user.cpp
    spvr_user_api.cpp
    spvr_stream.cpp
    spvr_user_device.cpp
    spvr_user_device_api.cpp
    spvr_event.cpp
    spvr_event_api.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
if(ANDROID)
#    find_package(Protobuf REQUIRED)
#    target_link_libraries(${PROJECT_NAME} tc_relay_message protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)
else ()
#    target_link_libraries(${PROJECT_NAME} tc_relay_message libprotobuf)
endif ()