#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "px_render/plugin_interface/px_plugin_interface.h"
#include "px_voice_call/voice_audio_endpoint.h"
#include "px_voice_call/voice_call_state.h"
#include "px_voice_call/voice_consent_decision_cache.h"
#include "px_voice_call/voice_packet_transport.h"

namespace px {

class VoiceCallPlugin final : public PxPluginInterface, public PxWebRtcVoicePcmSink {
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
    void DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;
    void OnNewClientConnected(
        const std::string& visitor_device_id, const std::string& stream_id,
        const std::string& conn_type) override;
    void OnClientDisconnected(
        const std::string& visitor_device_id, const std::string& stream_id) override;
    void OnWebRtcVoicePcm(
        const std::string& stream_id, const std::string& call_id,
        const int16_t* samples, size_t sample_count,
        int sample_rate, int channels) override;

private:
    void ProcessRequest(const std::shared_ptr<Message>& msg);
    void ProcessAudioFrame(const std::shared_ptr<Message>& msg);
    void RequestConsent(
        const std::string& visitor_device_id, const std::string& stream_id,
        const std::string& call_id, uint64_t request_id);
    void CancelConsent(
        const std::string& stream_id, const std::string& call_id,
        uint64_t request_id, const std::string& reason);
    void ApplyConsentDecision(const MsgVoiceCallConsentDecision& decision);
    void SendResponse(
        const std::string& device_id, const std::string& stream_id,
        const std::string& call_id, uint64_t request_id, bool accepted,
        const std::string& reason);
    void SendConfig(
        const std::string& device_id, const std::string& stream_id,
        const std::string& call_id);
    void QueueAudioFrame(
        const std::string& device_id, const std::string& stream_id,
        const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
        const std::vector<uint8_t>& opus);
    void DispatchAudioFrame(
        const std::string& device_id, const std::string& stream_id,
        const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
        const std::vector<uint8_t>& opus);
    void EndCall(const std::string& call_id, bool notify_remote, const std::string& reason);
    bool SetWebRtcVoiceAuthorization(
        const std::string& stream_id, const std::string& call_id,
        bool authorized);
    void SendWebRtcVoicePcm(
        const std::string& stream_id, const std::string& call_id,
        const int16_t* samples, size_t sample_count);
    [[nodiscard]] bool IsAuthenticatedSessionLocked(
        const std::string& device_id, const std::string& stream_id) const;

    mutable std::mutex mutex_;
    VoiceCallState state_;
    std::shared_ptr<VoiceAudioEndpoint> endpoint_;
    std::map<std::string, std::string> connected_clients_;
    std::map<std::string, std::string> connection_types_;
    std::string active_device_id_;
    std::string active_stream_id_;
    VoiceConsentDecisionCache decision_cache_;
    VoicePacketTransport packet_transport_;
    bool enabled_ = true;
};

}  // namespace px

PX_PLUGIN_EXPORT(px::VoiceCallPlugin)
