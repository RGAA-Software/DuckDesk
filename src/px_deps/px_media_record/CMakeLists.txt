cmake_minimum_required(VERSION 3.22)
project(px_media_record)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 共享录制核心：已编码音视频 remux 成 MP4（客户端与 render 端共用）
include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

add_library(px_media_record STATIC
        record_writer.cpp
)

target_include_directories(px_media_record PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 静态库的 PRIVATE 链接依赖在最终链接时自动传递给使用者
target_link_libraries(px_media_record PRIVATE ${FFMPEG_LIBRARIES})

# tests/ 目录可能尚未提交（此前被根 .gitignore 的裸 tests/ 规则误吞），存在才挂
if(TESTS_ENABLED AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
