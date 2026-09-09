#pragma once

#ifdef ANDROID

#include <cstdint>
#include <memory>

#include "sdk_video_decoder.h"
#include "platform/android/android_video_output.h"

namespace px {

class AndroidSoftwareVideoDecoder final : public VideoDecoder {
public:
    AndroidSoftwareVideoDecoder(const std::shared_ptr<ThunderSdk>& sdk, std::shared_ptr<AndroidVideoOutput> output);
    ~AndroidSoftwareVideoDecoder() override;

    int Init(const std::string& monitor_name, VideoType codec_type, int width, int height, const std::string& frame, EImageFormat image_format,
             bool ignore_hardware) override;
    Result<std::shared_ptr<RawImage>, int> Decode(std::span<const std::uint8_t> encoded) override;
    void Release() override;
    bool RefreshOutput() override;
    bool Ready() override;

private:
    class State;
    const std::shared_ptr<AndroidVideoOutput> output_{};
    std::unique_ptr<State> state_{};
};

} // namespace px

#endif
