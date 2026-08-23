#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "px_render/plugin_interface/px_plugin_interface.h"
#include "px_voice_call/voice_audio_endpoint.h"
#include "px_voice_call/voice_call_state.h"

namespace px {

class VoiceCallPlugin final : public PxPluginInterface {
public:
    std::string GetPluginId() override;
    std::string GetPluginName() override;
    std::string GetVersionName() override;
    uint32_t GetVersionCode() override;
    std::string GetPluginDescription() override;

    bool OnCreate(const PxPluginParam& param) override;
    bool OnStop() override;
    bool OnDestroy() override;
    void On1Second() override;
    void OnMessage(std::shared_ptr<Message> msg) override;
    void OnNewClientConnected(
        const std::string& visitor_device_id, const std::string& stream_id,
        const std::string& conn_type) override;
    void OnClientDisconnected(
        const std::string& visitor_device_id, const std::string& stream_id) override;

private:
    void ProcessRequest(const std::shared_ptr<Message>& msg);
    void ProcessAudioFrame(const std::shared_ptr<Message>& msg);
    void PromptIncoming(
        std::string device_id, std::string stream_id, std::string call_id,
        uint64_t request_id);
    void SendResponse(
        const std::string& device_id, const std::string& stream_id,
        const std::string& call_id, uint64_t request_id, bool accepted,
        const std::string& reason);
    void SendConfig(
        const std::string& device_id, const std::string& stream_id,
        const std::string& call_id);
    void SendAudioFrame(
        const std::string& device_id, const std::string& stream_id,
        const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
        const std::vector<uint8_t>& opus);
    void EndCall(const std::string& call_id, bool notify_remote, const std::string& reason);
    [[nodiscard]] bool IsAuthenticatedSessionLocked(
        const std::string& device_id, const std::string& stream_id) const;

    mutable std::mutex mutex_;
    VoiceCallState state_;
    std::shared_ptr<VoiceAudioEndpoint> endpoint_;
    std::map<std::string, std::string> connected_clients_;
    std::string active_device_id_;
    std::string active_stream_id_;
    bool last_decision_valid_ = false;
    bool last_decision_accepted_ = false;
    std::string last_decision_reason_;
    bool enabled_ = true;
};

}  // namespace px

PX_PLUGIN_EXPORT(px::VoiceCallPlugin)
