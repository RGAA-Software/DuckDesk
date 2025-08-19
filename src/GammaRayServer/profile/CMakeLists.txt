cmake_minimum_required(VERSION 3.26)
project(gr_profile_server)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DUNICODE -D_UNICODE)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if(WIN32)
    add_compile_options($<IF:$<CONFIG:Debug>,/MTd,/MT>)
    add_compile_options("/bigobj")
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
endif ()

find_package(Drogon CONFIG REQUIRED)
find_package(bsoncxx CONFIG REQUIRED)
find_package(mongocxx CONFIG REQUIRED)

#find_package(Protobuf REQUIRED)
#find_package(gRPC REQUIRED)
#set(PROTO_FILES grpc_profile.proto)
#protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
#
#### protobuf beg
#set(PROTOC ${VCPKG_ROOT}/installed/x64-windows/tools/protobuf/protoc.exe)
#set(SERVER_FILE ${PROTO_FILES})
#get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
#file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/api/protos)
#file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/api/protos)
#execute_process(COMMAND ${PROTOC} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/api/protos ${SERVER_FILE} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
#execute_process(COMMAND ${PROTOC} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} --grpc_out=${CMAKE_CURRENT_BINARY_DIR}/api/protos --plugin=protoc-gen-grpc=${grpc_cpp_plugin_location} ${SERVER_FILE} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
#set(RPC_SERVER_SRCS ${CMAKE_CURRENT_BINARY_DIR}/api/protos/grpc_profile.pb.cc ${CMAKE_CURRENT_BINARY_DIR}/api/protos/grpc_profile.grpc.pb.cc)
#### protobuf end

set(PROFILE_SRCS
        main.cpp
        #${RPC_SERVER_SRCS}
        gr_profile.cpp
        gr_profile_controller.cpp
        gr_profile_database.cpp
        gr_profile_context.cpp
        gr_profile_settings.cpp
)

add_executable(${PROJECT_NAME} ${PROFILE_SRCS})
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)
target_link_libraries(${PROJECT_NAME} PRIVATE $<IF:$<TARGET_EXISTS:mongo::bsoncxx_static>,mongo::bsoncxx_static,mongo::bsoncxx_shared>)
target_link_libraries(${PROJECT_NAME} PRIVATE $<IF:$<TARGET_EXISTS:mongo::mongocxx_static>,mongo::mongocxx_static,mongo::mongocxx_shared>)
#target_link_libraries(${PROJECT_NAME} PRIVATE gRPC::gpr gRPC::grpc gRPC::grpc++ gRPC::grpc++_alts)