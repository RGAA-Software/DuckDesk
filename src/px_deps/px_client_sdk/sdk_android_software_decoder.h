#pragma once

#ifdef ANDROID

#include <cstdint>
#include <memory>

#include "sdk_video_decoder.h"

namespace px {

class AndroidSoftwareVideoDecoder final : public VideoDecoder {
public:
    explicit AndroidSoftwareVideoDecoder(const std::shared_ptr<ThunderSdk>& sdk);
    ~AndroidSoftwareVideoDecoder() override;

    int Init(const std::string& monitor_name, int codec_type, int width, int height, const std::string& frame,
             void* surface, // NOLINT(gammaray-raw-pointer-boundary)
             int image_format, bool ignore_hardware) override;
    Result<std::shared_ptr<RawImage>, int> Decode(
        const std::uint8_t* data, // NOLINT(gammaray-raw-pointer-boundary)
        int size) override;
    void Release() override;
    bool UpdateRenderSurface(std::uintptr_t surface_handle) override;
    bool Ready() override;

private:
    class State;
    std::unique_ptr<State> state_{};
};

} // namespace px

#endif
