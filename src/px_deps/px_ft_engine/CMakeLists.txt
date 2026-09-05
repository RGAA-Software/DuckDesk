cmake_minimum_required(VERSION 3.22)
project(px_ft_engine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# rustdesk fs.rs/compress.rs 的 C++ 移植:双端共享文件传输引擎
# 依赖:px_message(proto 生成代码)+ miniz(块压缩)+ nlohmann_json(.digest 凭证)
find_package(miniz CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(px_ft_engine STATIC
    ft_path.cpp
    ft_compress.cpp
    ft_sha256.cpp
    ft_terminal.cpp
    transfer_job.cpp
    ft_engine.cpp
    ft_async_session.cpp
)

# Ninja cannot infer MSVC's localized /showIncludes output on every Windows
# installation.  Without explicit dependencies these objects can survive a
# protobuf regeneration and retain the old C++ message layout.  That is fatal
# for value types such as FileEntry: one object file may allocate the old
# sizeof(FileEntry) while another writes the regenerated, larger object.
set(PX_FT_PROTO_HEADERS
    ${PX_PROJECT_BINARY_PATH}/px_message/px_message.pb.h
    ${PX_PROJECT_BINARY_PATH}/px_message/px_file_transfer.pb.h
)
set_property(SOURCE
    ft_path.cpp
    ft_compress.cpp
    ft_sha256.cpp
    ft_terminal.cpp
    transfer_job.cpp
    ft_engine.cpp
    ft_async_session.cpp
    APPEND PROPERTY OBJECT_DEPENDS "${PX_FT_PROTO_HEADERS}"
)

target_include_directories(px_ft_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# px_message.pb.h / px_file_transfer.pb.h 生成目录
target_include_directories(px_ft_engine PUBLIC ${PX_PROJECT_BINARY_PATH}/px_message)

target_link_libraries(px_ft_engine PUBLIC px_message px_common)
target_link_libraries(px_ft_engine PRIVATE miniz::miniz nlohmann_json::nlohmann_json)

if(TESTS_ENABLED)
    add_subdirectory(tests)
endif()
