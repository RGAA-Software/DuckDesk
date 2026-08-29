#include "voice_call_plugin.h"

#include <span>

#include "voice_call_runtime.h"
#include "px_common_new/log.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {

VoiceCallPlugin::~VoiceCallPlugin() {
    if (runtime_) {
        runtime_->Shutdown("plugin_destroyed");
        runtime_.reset();
    }
}

std::string VoiceCallPlugin::GetPluginId() { return kVoiceCallPluginId; }
std::string VoiceCallPlugin::GetPluginName() { return "Voice Call"; }
std::string VoiceCallPlugin::GetVersionName() { return "1.1.0"; }
uint32_t VoiceCallPlugin::GetVersionCode() { return 110; }
std::string VoiceCallPlugin::GetPluginDescription() {
    return "Authenticated one-to-one voice calls using owned async media";
}

bool VoiceCallPlugin::OnCreate(const PxPluginParam& param) {
    if (!PxPluginInterface::OnCreate(param)) {
        return false;
    }
    bool enabled = true;
    if (HasParam("voice_call_enabled")) {
        enabled = GetConfigParam<bool>("voice_call_enabled");
    }
    runtime_ = VoiceCallRuntime::Make(
        enabled, std::weak_ptr<PxPluginContext>(plugin_context_));
    RefreshEventDelivery();
    LOGI("[VoiceCall] plugin ready, enabled={}, protocol=1, "
         "format=48000Hz mono 20ms Opus", enabled);
    return static_cast<bool>(runtime_);
}

bool VoiceCallPlugin::OnStop() {
    if (runtime_) {
        RefreshEventDelivery();
        runtime_->Shutdown("render_stopping");
    }
    return PxPluginInterface::OnStop();
}

bool VoiceCallPlugin::OnDestroy() {
    if (runtime_) {
        runtime_->Shutdown("render_destroyed");
        runtime_.reset();
    }
    return PxPluginInterface::OnDestroy();
}

void VoiceCallPlugin::On1Second() {
    PxPluginInterface::On1Second();
    if (runtime_) {
        RefreshEventDelivery();
        runtime_->On1Second();
    }
}

void VoiceCallPlugin::DispatchAppEvent(
    const std::shared_ptr<AppBaseEvent>& event) {
    PxPluginInterface::DispatchAppEvent(event);
    if (!runtime_ || !event ||
        event->type_ != AppBaseEvent::EType::kVoiceCallConsentDecision) {
        return;
    }
    RefreshEventDelivery();
    if (const auto decision =
            std::dynamic_pointer_cast<MsgVoiceCallConsentDecision>(event)) {
        runtime_->ApplyConsentDecision(*decision);
    }
}

void VoiceCallPlugin::OnNewClientConnected(
    const std::string& visitor_device_id,
    const std::string& stream_id,
    const std::string& connection_type) {
    PxPluginInterface::OnNewClientConnected(
        visitor_device_id, stream_id, connection_type);
    if (runtime_) {
        RefreshEventDelivery();
        runtime_->OnClientConnected(
            visitor_device_id, stream_id, connection_type);
    }
}

void VoiceCallPlugin::OnClientDisconnected(
    const std::string& visitor_device_id,
    const std::string& stream_id) {
    PxPluginInterface::OnClientDisconnected(visitor_device_id, stream_id);
    if (runtime_) {
        RefreshEventDelivery();
        runtime_->OnClientDisconnected(stream_id);
    }
}

void VoiceCallPlugin::OnMessage(std::shared_ptr<Message> message) {
    PxPluginInterface::OnMessage(message);
    if (runtime_) {
        RefreshEventDelivery();
        runtime_->OnMessage(message);
    }
}

void VoiceCallPlugin::OnWebRtcVoicePcm(
    const std::string& stream_id,
    const std::string& call_id,
    const int16_t* samples,
    size_t sample_count,
    int sample_rate,
    int channels) {
    // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): libwebrtc sink ABI
    if (runtime_ && samples && sample_count > 0) {
        runtime_->ReceiveWebRtcPcm(
            stream_id, call_id,
            std::span<const int16_t>(samples, sample_count),
            sample_rate, channels);
    }
}

void VoiceCallPlugin::RefreshEventDelivery() {
    if (!runtime_) {
        return;
    }
    const auto callback = event_cbk_;
    if (!callback) {
        runtime_->ClearEventDelivery();
        return;
    }
    runtime_->SetEventDelivery(
        [callback](const std::shared_ptr<PxPluginBaseEvent>& event) {
            callback(event);
        });
}

}  // namespace px
