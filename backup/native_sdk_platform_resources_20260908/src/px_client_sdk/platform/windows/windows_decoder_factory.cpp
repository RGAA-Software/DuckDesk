#include "windows_decoder_factory.h"
#include "sdk_video_decoder_factory.h"
#include "sdk_ffmpeg_decoder.h"
#include "sdk_ffmpeg_vulkan_decoder.h"
#include "sdk_ffmpeg_soft_decoder.h"
#include "thunder_sdk.h"
#include "px_common/log.h"

namespace px {
namespace {

class WindowsVideoDecoderFactory final : public VideoDecoderFactory {
  public:
    VideoDecoderCreation Create(const std::shared_ptr<ThunderSdk>& sdk, const VideoFrame& frame, const bool hardware_disabled) override {
        if (!sdk || !sdk->GetSdkParams())
            return {};
        // Vulkan's device context is populated when the client creates its view,
        // after SDK initialization. Read it at decoder creation, not factory construction.
        const auto params = sdk->GetSdkParams();
        std::shared_ptr<VideoDecoder> primary{};
        if (params->support_vulkan_) {
            primary = std::make_shared<FFmpegVulkanDecoder>(sdk);
        } else {
            primary = std::make_shared<FFmpegDecoder>(sdk);
        }
        if (auto ready = Initialize(std::move(primary), frame, hardware_disabled))
            return {.decoder = std::move(ready)};
        LOGW("Windows primary decoder initialization failed; trying FFmpeg software decoding");
        return {.decoder = Initialize(std::make_shared<FFmpegVideoDecoder>(sdk), frame, false)};
    }

    bool SupportsMultipleStreams() const noexcept override {
        return true;
    }
};

} // namespace

std::shared_ptr<VideoDecoderFactory> MakeWindowsVideoDecoderFactory() {
    return std::make_shared<WindowsVideoDecoderFactory>();
}

} // namespace px
