project(live_pusher)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} SHARED
        live_pusher_plugin.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_ROOT}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE px_common_new px_message px_plugin ${FFMPEG_LIBRARIES})
