#pragma once

#include <memory>

namespace px {

class ThunderSdk;
class VideoDecoder;
class VideoFrame;

struct VideoDecoderCreation final {
    std::shared_ptr<VideoDecoder> decoder{};
    bool disable_hardware{};
};

// Platform adapters are injected by the client composition root. The session
// knows neither concrete decoder classes nor output handles.
class VideoDecoderFactory {
  public:
    virtual ~VideoDecoderFactory() = default;
    [[nodiscard]] virtual VideoDecoderCreation Create(const std::shared_ptr<ThunderSdk>& sdk, const VideoFrame& first_frame,
                                                      bool hardware_disabled) = 0;
    [[nodiscard]] virtual bool SupportsMultipleStreams() const noexcept = 0;

  protected:
    [[nodiscard]] static std::shared_ptr<VideoDecoder> Initialize(std::shared_ptr<VideoDecoder> decoder, const VideoFrame& first_frame,
                                                                  bool ignore_hardware);
};

} // namespace px
