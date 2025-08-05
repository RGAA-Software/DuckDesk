
#pragma once

#include <string_view>

namespace tc
{

    const std::size_t kNumberOfSharedFrames = 2;
    const std::size_t kNumberOfDataPointers = 8;

    constexpr std::wstring_view kSharedTextureHandleNameFormat{L"Regame_{}_{}"};

    enum class VideoFrameType : std::uint32_t {
        kNone = 0, kYuv, kTexture
    };

    struct SharedVideoFrameInfo {
        std::uint64_t timestamp;
        VideoFrameType type;
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t format;
        std::uint64_t window;  // HWND
    };

    struct VideoFrameStats {
        volatile uint64_t timestamp;
        struct {
            std::uint64_t preprocess;
            std::uint64_t nvenc;
            std::uint64_t wait_rgb_mapping;
            std::uint64_t rgb_mapping;
            std::uint64_t yuv_convert;
            std::uint64_t total;
        } elapsed;
        struct {
            std::uint64_t acquire_buffer_pending;
            std::uint64_t acquire_buffer_successed;
        } count;
    };

    struct PackedVideoTextureFrame {
        VideoFrameStats stats;
        std::uint64_t instance_id;
        std::uint64_t texture_id;
    };

    struct SharedVideoTextureFrames {
        std::uint32_t data_size;  // sizeof(frames)
        PackedVideoTextureFrame frames[kNumberOfSharedFrames];
    };

    constexpr std::wstring_view kVideoStartedEventName{
            L"{75B053EC-7CF7-4608-961F-3D2663F3FB2D}"};
    constexpr std::wstring_view kVideoStoppedEventName{
            L"{6D27BAD8-A314-4421-8CC1-1285E940631D}"};
    constexpr std::wstring_view kSharedVideoFrameInfoFileMappingName{
            L"{C6854F73-DE64-40E1-BECF-07B63EBF89A2}"};

    constexpr std::wstring_view kDoNotPresentEventName{
            L"{E7E2141B-4312-4609-BDEB-5B722CC01B96}"};

}  // namespace tc
