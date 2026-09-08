#include "windows_decoder_factory.h"
#include "windows_video_resources.h"
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
    explicit WindowsVideoDecoderFactory(std::shared_ptr<const WindowsVideoResources> resources) : resources_(std::move(resources)) {}

    VideoDecoderCreation Create(const std::shared_ptr<ThunderSdk>& sdk, const VideoFrame& frame, const bool hardware_disabled) override {
        if (!sdk)
            return {};
        std::shared_ptr<VideoDecoder> primary{};
        if (resources_->use_vulkan) {
            primary = std::make_shared<FFmpegVulkanDecoder>(sdk, resources_);
        } else {
            primary = std::make_shared<FFmpegDecoder>(sdk, resources_);
        }
        if (auto ready = Initialize(std::move(primary), frame, hardware_disabled))
            return {.decoder = std::move(ready)};
        LOGW("Windows primary decoder initialization failed; trying FFmpeg software decoding");
        return {.decoder = Initialize(std::make_shared<FFmpegVideoDecoder>(sdk), frame, false)};
    }

    bool SupportsMultipleStreams() const noexcept override {
        return true;
    }

  private:
    const std::shared_ptr<const WindowsVideoResources> resources_{};
};

} // namespace

std::shared_ptr<VideoDecoderFactory> MakeWindowsVideoDecoderFactory(std::shared_ptr<const WindowsVideoResources> resources) {
    if (!resources)
        return {};
    return std::make_shared<WindowsVideoDecoderFactory>(std::move(resources));
}

} // namespace px
