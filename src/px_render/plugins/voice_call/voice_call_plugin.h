#pragma once

#include <memory>
#include <string>

#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px {

class VoiceCallRuntime;

class VoiceCallPlugin final : public PxPluginInterface,
                              public PxWebRtcVoicePcmSink {
public:
    ~VoiceCallPlugin() override;

    std::string GetPluginId() override;
    std::string GetPluginName() override;
    std::string GetVersionName() override;
    uint32_t GetVersionCode() override;
    std::string GetPluginDescription() override;
    bool OnCreate(const PxPluginParam& param) override;
    bool OnStop() override;
    bool OnDestroy() override;
    void On1Second() override;
    void OnMessage(std::shared_ptr<Message> message) override;
    void DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;
    void OnNewClientConnected(
        const std::string& visitor_device_id,
        const std::string& stream_id,
        const std::string& connection_type) override;
    void OnClientDisconnected(
        const std::string& visitor_device_id,
        const std::string& stream_id) override;
    void OnWebRtcVoicePcm(
        const std::string& stream_id,
        const std::string& call_id,
        const int16_t* samples, // NOLINT(gammaray-raw-pointer-boundary): libwebrtc sink ABI
        size_t sample_count,
        int sample_rate,
        int channels) override;

private:
    void RefreshEventDelivery();

    std::shared_ptr<VoiceCallRuntime> runtime_;
};

}  // namespace px

PX_PLUGIN_EXPORT(px::VoiceCallPlugin)
