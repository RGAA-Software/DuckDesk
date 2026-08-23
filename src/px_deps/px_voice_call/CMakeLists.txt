project(px_voice_call)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(px_voice_call
    voice_call_state.cpp
    voice_audio_endpoint.cpp
)
target_include_directories(px_voice_call PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(px_voice_call PUBLIC px_opus_codec_new
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)

add_executable(test_voice_call
    tests/test_voice_call.cpp
)
target_include_directories(test_voice_call PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_voice_call PRIVATE px_voice_call GTest::gtest GTest::gtest_main)
