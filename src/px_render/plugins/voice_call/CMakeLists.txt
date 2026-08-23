project(voice_call)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} SHARED voice_call_plugin.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE
    px_common_new px_message px_plugin px_voice_call)
