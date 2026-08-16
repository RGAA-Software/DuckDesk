cmake_minimum_required(VERSION 3.22)
project(px_ft_engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# rustdesk fs.rs/compress.rs 的 C++ 移植:双端共享文件传输引擎
# 依赖:px_message(proto 生成代码)+ miniz(块压缩)+ nlohmann_json(.digest 凭证)
find_package(miniz CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(px_ft_engine STATIC
    ft_path.cpp
    ft_compress.cpp
    transfer_job.cpp
    ft_engine.cpp
)

target_include_directories(px_ft_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# px_message.pb.h / px_file_transfer.pb.h 生成目录
target_include_directories(px_ft_engine PUBLIC ${PX_PROJECT_BINARY_PATH}/px_message_new)

target_link_libraries(px_ft_engine PUBLIC px_message)
target_link_libraries(px_ft_engine PRIVATE miniz::miniz nlohmann_json::nlohmann_json)

if(TESTS_ENABLED)
    add_subdirectory(tests)
endif()
