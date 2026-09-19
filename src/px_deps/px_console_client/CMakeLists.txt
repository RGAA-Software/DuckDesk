project(px_console_client)

set(SRCS
    console_api.cpp
    console_user.cpp
    console_user_api.cpp
    console_stream.cpp
    console_user_device.cpp
    console_user_device_api.cpp
    console_user_app_api.cpp
    console_http_client.cpp
    console_resource_api.cpp
    deployment_identity.cpp
)

add_library(${PROJECT_NAME} ${SRCS})
target_link_libraries(${PROJECT_NAME} PUBLIC px_common PRIVATE OpenSSL::Crypto)
if(ANDROID)
#    find_package(Protobuf REQUIRED)
#    target_link_libraries(${PROJECT_NAME} px_relay_message protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)
else ()
#    target_link_libraries(${PROJECT_NAME} px_relay_message libprotobuf)
endif ()

if(BUILD_TESTING AND TARGET GTest::gtest)
    add_executable(test_deployment_identity tests/deployment_identity_tests.cpp)
    target_link_libraries(test_deployment_identity PRIVATE px_console_client GTest::gtest GTest::gtest_main OpenSSL::Crypto)
    add_test(NAME test_deployment_identity COMMAND test_deployment_identity)
endif()
