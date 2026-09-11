cmake_minimum_required(VERSION 3.26)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PixelsMediaTransport LANGUAGES C CXX)
    include(CTest)
endif()

get_filename_component(media_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(media_upstream "${media_repo_root}/third_party/moonlight_media_reference")
set(media_nanors "${media_upstream}/nanors")
set(media_rs_symbols
    reed_solomon_init reed_solomon_new_static reed_solomon_new reed_solomon_release
    reed_solomon_encode reed_solomon_decode reed_solomon_aligned_alloc reed_solomon_free reed_solomon_padded_size)
set(media_rs_definitions)
foreach(symbol IN LISTS media_rs_symbols)
    list(APPEND media_rs_definitions "${symbol}=pixels_upstream_${symbol}")
endforeach()

add_library(pixels_media_nanors STATIC
    "${media_nanors}/rs.c"
    "${media_nanors}/deps/obl/oblas_common.c"
    "${media_nanors}/deps/obl/oblas_lite.c")
target_include_directories(pixels_media_nanors SYSTEM PUBLIC "${media_nanors}" "${media_nanors}/deps" "${media_nanors}/deps/obl")
target_compile_definitions(pixels_media_nanors PRIVATE ${media_rs_definitions})
set_target_properties(pixels_media_nanors PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(pixels_media_transport STATIC nanors_codec.cpp video_packetizer.cpp video_receive_queue.cpp media_wire.cpp video_stream.cpp audio_stream.cpp)
add_library(pixels::media_transport ALIAS pixels_media_transport)
target_compile_features(pixels_media_transport PUBLIC cxx_std_23)
target_include_directories(pixels_media_transport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_definitions(pixels_media_transport PRIVATE ${media_rs_definitions})
target_link_libraries(pixels_media_transport PRIVATE pixels_media_nanors)
set_target_properties(pixels_media_transport PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

if(BUILD_TESTING OR TESTS_ENABLED)
    find_package(GTest CONFIG REQUIRED)
    add_executable(test_upstream_media_reference
        tests/test_upstream_media_reference.cpp
        tests/test_media_stream.cpp
        tests/test_send_policy.cpp
        tests/test_packet_timing.cpp
        tests/upstream_video_oracle.cpp
        "${media_upstream}/src/RtpVideoQueue.c"
        "${media_upstream}/src/RtpAudioQueue.c")
    target_compile_features(test_upstream_media_reference PRIVATE cxx_std_23)
    target_compile_definitions(test_upstream_media_reference PRIVATE NDEBUG HAS_SOCKLEN_T ${media_rs_definitions})
    target_include_directories(test_upstream_media_reference SYSTEM PRIVATE "${media_upstream}/src" "${media_upstream}/enet/include")
    target_link_libraries(test_upstream_media_reference PRIVATE pixels_media_transport pixels_media_nanors GTest::gtest GTest::gtest_main)
    set_target_properties(test_upstream_media_reference PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_test(NAME upstream_media_reference COMMAND test_upstream_media_reference)
    set_tests_properties(upstream_media_reference PROPERTIES TIMEOUT 60)
endif()
