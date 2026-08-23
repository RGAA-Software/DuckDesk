project(px_voice_call)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # Keep libwebrtc in a DLL boundary. The vendored archive embeds a protobuf
    # runtime that must not be co-linked with the product's vcpkg protobuf.
    add_library(px_voice_apm SHARED voice_audio_processing.cpp)
    target_compile_definitions(px_voice_apm PRIVATE
        PX_VOICE_APM_EXPORTS WEBRTC_WIN=1 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(px_voice_apm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # Prefer libwebrtc's matching Abseil headers over the product-wide vcpkg
    # version; its archive and headers are a single ABI unit.
    target_include_directories(px_voice_apm BEFORE PRIVATE
        ${WEBRTC_ROOT}
        ${WEBRTC_ROOT}/include
        ${WEBRTC_ROOT}/include/third_party/abseil-cpp)
    target_link_libraries(px_voice_apm PRIVATE
        ${WEBRTC_ROOT}/x64/webrtc.lib
        Winmm ws2_32 Secur32 Crypt32 Msdmo dmoguids wmcodecdspuuid
        Iphlpapi dxgi d3d11 d3d12 dxguid Strmiids Quartz Ole32 Shcore Dwmapi)
endif()

add_library(px_voice_call
    voice_call_state.cpp
    voice_consent_decision_cache.cpp
    voice_jitter_buffer.cpp
    voice_packet_transport.cpp
    voice_audio_backend.cpp
    voice_audio_backend_sdl.cpp
    voice_audio_endpoint.cpp
)
target_include_directories(px_voice_call PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(px_voice_call PUBLIC px_opus_codec_new
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
if(WIN32)
    target_sources(px_voice_call PRIVATE
        voice_audio_backend_wasapi.cpp
        ${CMAKE_SOURCE_DIR}/src/px_render/plugins/was_audio_capture/third_party/miniaudio/miniaudio.c
        ${CMAKE_SOURCE_DIR}/src/px_render/plugins/was_audio_capture/third_party/miniaudio/miniaudio_desktop_process_loopback.cpp)
    target_include_directories(px_voice_call PRIVATE
        ${CMAKE_SOURCE_DIR}/src/px_render/plugins/was_audio_capture/third_party)
    target_compile_definitions(px_voice_call PRIVATE
        MA_WASAPI_DEFAULT_ROLE=ma_eCommunications)
    target_link_libraries(px_voice_call PUBLIC px_voice_apm Ole32 Mmdevapi)
endif()

add_executable(test_voice_call
    tests/test_voice_call.cpp
)
target_include_directories(test_voice_call PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_voice_call PRIVATE
    px_voice_call px_rp_message GTest::gtest GTest::gtest_main)
if(WIN32)
    target_link_libraries(test_voice_call PRIVATE Psapi)
endif()

if(BUILD_TESTING)
    add_test(NAME voice_call_core COMMAND test_voice_call)
endif()
