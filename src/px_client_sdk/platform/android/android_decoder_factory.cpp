#include "android_decoder_factory.h"
#include "android_video_output.h"
#include "sdk_video_decoder_factory.h"
#include "sdk_mediacodec_video_decoder.h"
#include "sdk_android_software_decoder.h"
#include "px_common/log.h"

namespace px {
namespace {

class AndroidVideoDecoderFactory final : public VideoDecoderFactory {
  public:
    AndroidVideoDecoderFactory(std::shared_ptr<AndroidVideoOutput> output, const bool prefer_software)
        : output_(std::move(output)), prefer_software_(prefer_software) {}

    VideoDecoderCreation Create(const std::shared_ptr<ThunderSdk>& sdk, const VideoFrame& frame, const bool hardware_disabled) override {
        if (!sdk || !output_->Snapshot())
            return {};
        if (prefer_software_ || hardware_disabled) {
            return {.decoder = Initialize(std::make_shared<AndroidSoftwareVideoDecoder>(sdk, output_), frame, true)};
        }
        if (auto ready = Initialize(std::make_shared<MediacodecVideoDecoder>(sdk, output_), frame, false)) {
            return {.decoder = std::move(ready)};
        }
        LOGW("MediaCodec initialization failed; falling back to FFmpeg software decoding");
        return {.decoder = Initialize(std::make_shared<AndroidSoftwareVideoDecoder>(sdk, output_), frame, true), .disable_hardware = true};
    }

    bool SupportsMultipleStreams() const noexcept override {
        return false;
    }

  private:
    const std::shared_ptr<AndroidVideoOutput> output_{};
    const bool prefer_software_{};
};

} // namespace

std::shared_ptr<VideoDecoderFactory> MakeAndroidVideoDecoderFactory(std::shared_ptr<AndroidVideoOutput> output, const bool prefer_software) {
    if (!output)
        return {};
    return std::make_shared<AndroidVideoDecoderFactory>(std::move(output), prefer_software);
}

} // namespace px
