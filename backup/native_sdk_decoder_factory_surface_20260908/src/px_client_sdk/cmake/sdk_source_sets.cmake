# Build inputs only: loading the shared source sets does not select a host OS.
# The session layer still contains legacy platform-aware public types and decoder
# dispatch. Keep it separate rather than describing it as a portable core.
set(PX_SDK_CORE_SOURCES
    sdk_net_client.cpp
    sdk_stream_helper.cpp
    sdk_cast_receiver.cpp
    sdk_timer.cpp
    sdk_statistics.cpp
    sdk_errors.cpp
    connection/connection.cpp
    connection/udp_direct_connection.cpp
    connection/ws_connection.cpp
    connection/wss_connection.cpp
)

set(PX_SDK_SESSION_SOURCES
    thunder_sdk.cpp
    sdk_video_decoder.cpp
    video_decode_thread_task.cpp
    gl/raw_image.cpp
)

function(px_sdk_platform_sources output platform)
    if(platform STREQUAL "windows")
        set(sources sdk_ffmpeg_decoder.cpp sdk_ffmpeg_vulkan_decoder.cpp sdk_ffmpeg_soft_decoder.cpp)
    elseif(platform STREQUAL "android")
        set(sources sdk_mediacodec_video_decoder.cpp sdk_android_software_decoder.cpp)
    else()
        message(FATAL_ERROR "No SDK decoder adapter for '${platform}'; implement a real platform adapter before building a full client")
    endif()
    set(${output} "${sources}" PARENT_SCOPE)
endfunction()
