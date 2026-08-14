project(tc_profile_client)

set(SRCS
    profile_api.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
if(ANDROID)
#    find_package(Protobuf REQUIRED)
#    target_link_libraries(${PROJECT_NAME} tc_relay_message protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)
else ()
#    target_link_libraries(${PROJECT_NAME} tc_relay_message libprotobuf)
endif ()