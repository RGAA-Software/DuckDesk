#pragma once

#include <memory>

#include "px_render/plugin_interface/px_audio_encoder_plugin.h"

namespace px {

class Data;
class OpusEncoderRuntime;

class OpusEncoderPlugin final : public PxAudioEncoderPlugin {
public:
    ~OpusEncoderPlugin() override;

    std::string GetPluginId() override;
    std::string GetPluginName() override;
    std::string GetVersionName() override;
    uint32_t GetVersionCode() override;
    std::string GetPluginDescription() override;
    void On1Second() override;
    bool OnCreate(const PxPluginParam& param) override;
    bool OnStop() override;
    bool OnDestroy() override;
    void Encode(
        const std::shared_ptr<Data>& data,
        int sample_rate,
        int channels,
        int bits) override;

private:
    void RefreshDelivery();
    std::shared_ptr<OpusEncoderRuntime> runtime_;
};

}  // namespace px

PX_PLUGIN_EXPORT(px::OpusEncoderPlugin)
