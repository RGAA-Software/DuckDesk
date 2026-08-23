#include "voice_call_plugin.h"

#include <chrono>
#include <utility>

#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {
namespace {

uint64_t MonotonicMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

std::string VoiceCallPlugin::GetPluginId() { return kVoiceCallPluginId; }
std::string VoiceCallPlugin::GetPluginName() { return "Voice Call"; }
std::string VoiceCallPlugin::GetVersionName() { return "1.0.0"; }
uint32_t VoiceCallPlugin::GetVersionCode() { return 100; }
std::string VoiceCallPlugin::GetPluginDescription() {
    return "Authenticated one-to-one voice calls using independent Opus media";
}

bool VoiceCallPlugin::OnCreate(const PxPluginParam& param) {
    PxPluginInterface::OnCreate(param);
    if (HasParam("voice_call_enabled")) {
        enabled_ = GetConfigParam<bool>("voice_call_enabled");
    }
    LOGI("[VoiceCall] plugin ready, enabled={}, protocol=1, format=48000Hz mono 20ms Opus",
         enabled_);
    return true;
}

bool VoiceCallPlugin::OnStop() {
    EndCall(state_.CallId(), false, "render_stopping");
    return PxPluginInterface::OnStop();
}

bool VoiceCallPlugin::OnDestroy() {
    EndCall(state_.CallId(), false, "render_destroyed");
    return PxPluginInterface::OnDestroy();
}

void VoiceCallPlugin::On1Second() {
    PxPluginInterface::On1Second();
    std::string expired_call;
    std::string device_id;
    std::string stream_id;
    uint64_t request_id = 0;
    {
        std::scoped_lock lock(mutex_);
        if (state_.Phase() != VoiceCallPhase::kIncomingPending) {
            return;
        }
        expired_call = state_.CallId();
        request_id = state_.RequestId();
        device_id = active_device_id_;
        stream_id = active_stream_id_;
        if (!state_.Expire(MonotonicMillis())) {
            return;
        }
        decision_cache_.Put(expired_call, request_id, false, "timeout", MonotonicMillis());
        active_device_id_.clear();
        active_stream_id_.clear();
    }
    if (!expired_call.empty()) {
        CancelConsent(stream_id, expired_call, request_id, "timeout");
        SendResponse(device_id, stream_id, expired_call, request_id, false, "timeout");
    }
}

void VoiceCallPlugin::DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
    PxPluginInterface::DispatchAppEvent(event);
    if (!event || event->type_ != AppBaseEvent::EType::kVoiceCallConsentDecision) {
        return;
    }
    const auto decision = std::dynamic_pointer_cast<MsgVoiceCallConsentDecision>(event);
    if (decision) {
        ApplyConsentDecision(*decision);
    }
}

void VoiceCallPlugin::OnNewClientConnected(
    const std::string& visitor_device_id, const std::string& stream_id,
    const std::string& conn_type) {
    PxPluginInterface::OnNewClientConnected(visitor_device_id, stream_id, conn_type);
    if (visitor_device_id.empty() || stream_id.empty()) {
        return;
    }
    std::scoped_lock lock(mutex_);
    connected_clients_[stream_id] = visitor_device_id;
    connection_types_[stream_id] = conn_type;
}

void VoiceCallPlugin::OnClientDisconnected(
    const std::string& visitor_device_id, const std::string& stream_id) {
    PxPluginInterface::OnClientDisconnected(visitor_device_id, stream_id);
    std::string call_id;
    {
        std::scoped_lock lock(mutex_);
        connected_clients_.erase(stream_id);
        connection_types_.erase(stream_id);
        if (stream_id == active_stream_id_) {
            call_id = state_.CallId();
        }
    }
    if (!call_id.empty()) {
        EndCall(call_id, false, "disconnect");
    }
}

void VoiceCallPlugin::OnMessage(std::shared_ptr<Message> msg) {
    PxPluginInterface::OnMessage(msg);
    if (!msg) {
        return;
    }
    if (msg->type() >= kVoiceCallRequest && msg->type() <= kVoiceAudioConfig) {
        LOGI("[VoiceCall] envelope received, type={}, device={}, stream={}",
             static_cast<int>(msg->type()), msg->device_id(), msg->stream_id());
    }
    if (!enabled_) {
        if (msg->type() == kVoiceCallRequest && msg->voice_call_request().connect()) {
            const auto& request = msg->voice_call_request();
            bool authenticated = false;
            {
                std::scoped_lock lock(mutex_);
                authenticated = IsAuthenticatedSessionLocked(
                    msg->device_id(), msg->stream_id());
            }
            if (authenticated) {
                SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                             request.request_id(), false, "disabled_by_policy");
            }
        }
        return;
    }
    if (msg->type() == kVoiceCallRequest) {
        ProcessRequest(msg);
    } else if (msg->type() == kVoiceAudioFrame) {
        ProcessAudioFrame(msg);
    } else if (msg->type() == kVoiceAudioConfig) {
        const auto& config = msg->voice_audio_config();
        bool active_call = false;
        {
            std::scoped_lock lock(mutex_);
            active_call = IsAuthenticatedSessionLocked(
                              msg->device_id(), msg->stream_id()) &&
                msg->stream_id() == active_stream_id_ &&
                state_.IsMediaAllowed(config.call_id());
        }
        if (!active_call) {
            return;
        }
        if (config.sample_rate() != VoiceAudioEndpoint::kSampleRate ||
            config.channels() != VoiceAudioEndpoint::kChannels ||
            config.frame_ms() != VoiceAudioEndpoint::kFrameMs) {
            LOGW("[VoiceCall] incompatible config dropped, stream={}, call={}",
                 msg->stream_id(), VoiceCallLogId(config.call_id()));
        }
    }
}

void VoiceCallPlugin::ProcessRequest(const std::shared_ptr<Message>& msg) {
    const auto& request = msg->voice_call_request();
    LOGI("[VoiceCall] request received, connect={}, call={}, request={}, stream={}",
         request.connect(),
         request.call_id().size() <= VoiceCallState::kMaxCallIdBytes
             ? VoiceCallLogId(request.call_id()) : "invalid_length",
         request.request_id(), msg->stream_id());
    if (!request.connect()) {
        {
            std::scoped_lock lock(mutex_);
            if (!IsAuthenticatedSessionLocked(msg->device_id(), msg->stream_id()) ||
                msg->stream_id() != active_stream_id_ ||
                request.call_id() != state_.CallId()) {
                return;
            }
        }
        EndCall(request.call_id(), false, "remote_hangup");
        return;
    }

    IncomingVoiceCallResult result;
    bool authenticated = false;
    bool has_cached_decision = false;
    bool cached_accepted = false;
    std::string cached_reason;
    std::string visitor_device_id;
    {
        std::scoped_lock lock(mutex_);
        authenticated = IsAuthenticatedSessionLocked(
            msg->device_id(), msg->stream_id());
        if (!authenticated) {
            LOGW("[VoiceCall] unauthenticated request dropped, stream={}", msg->stream_id());
            return;
        }
        visitor_device_id = connected_clients_[msg->stream_id()];
        const auto now_ms = MonotonicMillis();
        if (const auto cached = decision_cache_.Find(
                request.call_id(), request.request_id(), now_ms)) {
            result = IncomingVoiceCallResult::kDuplicate;
            has_cached_decision = true;
            cached_accepted = cached->accepted;
            cached_reason = cached->reason;
        }
        else {
            result = state_.BeginIncoming(
                request.call_id(), request.request_id(), now_ms);
        }
        if (result == IncomingVoiceCallResult::kPending) {
            active_device_id_ = msg->device_id();
            active_stream_id_ = msg->stream_id();
        }
    }

    if (result == IncomingVoiceCallResult::kInvalid) {
        SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                     request.request_id(), false, "invalid_request");
    } else if (result == IncomingVoiceCallResult::kBusy) {
        SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                     request.request_id(), false, "busy");
    } else if (result == IncomingVoiceCallResult::kDuplicate && has_cached_decision) {
        SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                     request.request_id(), cached_accepted, cached_reason);
    } else if (result == IncomingVoiceCallResult::kPending) {
        LOGI("[VoiceCall] request pending user consent, call={}, stream={}",
             VoiceCallLogId(request.call_id()), msg->stream_id());
        RequestConsent(visitor_device_id, msg->stream_id(), request.call_id(),
                       request.request_id());
    }
}

void VoiceCallPlugin::RequestConsent(
    const std::string& visitor_device_id, const std::string& stream_id,
    const std::string& call_id, uint64_t request_id) {
    auto event = std::make_shared<PxPluginVoiceCallConsentEvent>();
    event->show_ = true;
    event->visitor_device_id_ = visitor_device_id;
    event->stream_id_ = stream_id;
    event->call_id_ = call_id;
    event->request_id_ = request_id;
    event->expires_at_unix_ms_ = TimeUtil::GetCurrentTimestamp() +
        VoiceCallState::kRequestTimeoutMs;
    LOGI("[VoiceCall] dispatching consent request to renderer, call={}, stream={}, request={}",
         VoiceCallLogId(call_id), stream_id, request_id);
    CallbackEvent(event);
}

void VoiceCallPlugin::CancelConsent(
    const std::string& stream_id, const std::string& call_id,
    uint64_t request_id, const std::string& reason) {
    if (stream_id.empty() || call_id.empty() || request_id == 0) {
        return;
    }
    auto event = std::make_shared<PxPluginVoiceCallConsentEvent>();
    event->show_ = false;
    event->stream_id_ = stream_id;
    event->call_id_ = call_id;
    event->request_id_ = request_id;
    event->reason_ = reason;
    LOGI("[VoiceCall] dispatching consent cancel to renderer, call={}, stream={}, request={}, reason={}",
         VoiceCallLogId(call_id), stream_id, request_id, reason);
    CallbackEvent(event);
}

void VoiceCallPlugin::ApplyConsentDecision(const MsgVoiceCallConsentDecision& decision) {
    if (decision.stream_id_.empty() || decision.call_id_.empty() ||
        decision.request_id_ == 0) {
        return;
    }

    std::string device_id;
    bool rejected = false;
    bool expired = false;
    // The renderer may synthesize a trusted fail-closed reason (for example
    // panel_unavailable). Preserve it so the caller can distinguish a local
    // user rejection from an unavailable authorization surface.
    std::string reject_reason = decision.reason_.empty() ? "rejected" : decision.reason_;
    {
        std::scoped_lock lock(mutex_);
        if (state_.Phase() != VoiceCallPhase::kIncomingPending ||
            state_.CallId() != decision.call_id_ ||
            state_.RequestId() != decision.request_id_ ||
            active_stream_id_ != decision.stream_id_ ||
            !connected_clients_.contains(decision.stream_id_)) {
            LOGW("[VoiceCall] stale or forged panel decision dropped, stream={}, call={}",
                 decision.stream_id_, VoiceCallLogId(decision.call_id_));
            return;
        }
        device_id = active_device_id_;
        expired = state_.Expire(MonotonicMillis());
        if (expired) {
            decision_cache_.Put(decision.call_id_, decision.request_id_, false,
                                "timeout", MonotonicMillis());
            active_device_id_.clear();
            active_stream_id_.clear();
        }
        else if (!decision.accepted_) {
            rejected = state_.RejectIncoming(decision.call_id_, decision.request_id_);
            if (rejected) {
                decision_cache_.Put(decision.call_id_, decision.request_id_, false,
                                    reject_reason, MonotonicMillis());
                active_device_id_.clear();
                active_stream_id_.clear();
            }
        }
    }
    if (expired) {
        CancelConsent(decision.stream_id_, decision.call_id_, decision.request_id_, "timeout");
        SendResponse(device_id, decision.stream_id_, decision.call_id_,
                     decision.request_id_, false, "timeout");
        return;
    }
    if (rejected) {
        CancelConsent(decision.stream_id_, decision.call_id_, decision.request_id_, reject_reason);
        SendResponse(device_id, decision.stream_id_, decision.call_id_,
                     decision.request_id_, false, reject_reason);
        return;
    }
    if (!decision.accepted_) {
        return;
    }

    bool is_webrtc = false;
    {
        std::scoped_lock lock(mutex_);
        const auto type = connection_types_.find(decision.stream_id_);
        is_webrtc = type != connection_types_.end() && type->second == "RTC";
    }
    auto endpoint = std::make_shared<VoiceAudioEndpoint>();
    const std::weak_ptr<VoiceAudioEndpoint> weak_endpoint = endpoint;
    std::string error;
    const bool started = endpoint->Start(
        [this, device_id, stream_id = decision.stream_id_, call_id = decision.call_id_, is_webrtc](
            uint32_t sequence, uint64_t capture_time_ms,
            const std::vector<uint8_t>& opus) {
            if (!is_webrtc) {
                QueueAudioFrame(device_id, stream_id, call_id, sequence, capture_time_ms, opus);
            }
        }, &error,
        [this, call_id = decision.call_id_, weak_endpoint](const std::string& reason) {
            PostWorkTask([this, call_id, weak_endpoint, reason]() {
                const auto expected_endpoint = weak_endpoint.lock();
                bool still_active = false;
                {
                    std::scoped_lock lock(mutex_);
                    still_active = expected_endpoint &&
                        state_.IsMediaAllowed(call_id) &&
                        endpoint_ == expected_endpoint;
                }
                if (still_active) {
                    EndCall(call_id, true,
                            reason.empty() ? "device_lost" : reason);
                }
            });
        },
        [this, stream_id = decision.stream_id_, call_id = decision.call_id_, is_webrtc](
            const int16_t* samples, size_t sample_count) {
            if (is_webrtc) {
                SendWebRtcVoicePcm(stream_id, call_id, samples, sample_count);
            }
        });
    if (!started) {
        bool no_mic = false;
        {
            std::scoped_lock lock(mutex_);
            no_mic = state_.RejectIncoming(decision.call_id_, decision.request_id_);
            if (no_mic) {
                decision_cache_.Put(decision.call_id_, decision.request_id_, false,
                                    "no_mic", MonotonicMillis());
                active_device_id_.clear();
                active_stream_id_.clear();
            }
        }
        if (no_mic) {
            LOGE("[VoiceCall] audio endpoint failed after local consent: {}", error);
            CancelConsent(decision.stream_id_, decision.call_id_, decision.request_id_, "no_mic");
            SendResponse(device_id, decision.stream_id_, decision.call_id_,
                         decision.request_id_, false, "no_mic");
        }
        return;
    }
    const auto backend_info = endpoint->BackendInfo();
    LOGI("[VoiceCall] audio backend={}, capture={}, playout={}, apm=aec+ns+agc",
         backend_info.backend, backend_info.capture_device, backend_info.playout_device);

    bool accepted = false;
    bool expired_while_starting = false;
    {
        std::scoped_lock lock(mutex_);
        if (state_.Phase() == VoiceCallPhase::kIncomingPending &&
            state_.CallId() == decision.call_id_ &&
            state_.RequestId() == decision.request_id_ &&
            active_stream_id_ == decision.stream_id_ &&
            connected_clients_.contains(decision.stream_id_)) {
            expired_while_starting = state_.Expire(MonotonicMillis());
            if (!expired_while_starting) {
                accepted = state_.AcceptIncoming(decision.call_id_, decision.request_id_);
            }
            if (accepted) {
                endpoint_ = endpoint;
                decision_cache_.Put(decision.call_id_, decision.request_id_, true,
                                    "", MonotonicMillis());
            }
            else if (expired_while_starting) {
                decision_cache_.Put(decision.call_id_, decision.request_id_, false,
                                    "timeout", MonotonicMillis());
                active_device_id_.clear();
                active_stream_id_.clear();
            }
        }
    }
    if (!accepted) {
        endpoint->Stop();
        if (expired_while_starting) {
            CancelConsent(decision.stream_id_, decision.call_id_, decision.request_id_, "timeout");
            SendResponse(device_id, decision.stream_id_, decision.call_id_,
                         decision.request_id_, false, "timeout");
        }
        return;
    }
    if (is_webrtc && !SetWebRtcVoiceAuthorization(
            decision.stream_id_, decision.call_id_, true)) {
        LOGE("[VoiceCall] WebRTC voice media path unavailable, stream={}, call={}",
             decision.stream_id_, VoiceCallLogId(decision.call_id_));
        EndCall(decision.call_id_, false, "webrtc_audio_unavailable");
        SendResponse(device_id, decision.stream_id_, decision.call_id_,
                     decision.request_id_, false, "webrtc_audio_unavailable");
        return;
    }
    if (!is_webrtc && !packet_transport_.Start(
            [this, device_id, stream_id = decision.stream_id_, call_id = decision.call_id_](
                const VoiceTransportPacket& packet) {
                DispatchAudioFrame(
                    device_id, stream_id, call_id, packet.sequence,
                    packet.capture_time_ms, packet.opus);
            })) {
        EndCall(decision.call_id_, false, "transport_unavailable");
        SendResponse(device_id, decision.stream_id_, decision.call_id_,
                     decision.request_id_, false, "transport_unavailable");
        return;
    }
    CancelConsent(decision.stream_id_, decision.call_id_, decision.request_id_, "accepted");
    SendResponse(device_id, decision.stream_id_, decision.call_id_,
                 decision.request_id_, true, "");
    SendConfig(device_id, decision.stream_id_, decision.call_id_);
    LOGI("[VoiceCall] accepted by px_panel, stream={}, call={}",
         decision.stream_id_, VoiceCallLogId(decision.call_id_));
}

void VoiceCallPlugin::OnWebRtcVoicePcm(
    const std::string& stream_id, const std::string& call_id,
    const int16_t* samples, size_t sample_count,
    int sample_rate, int channels) {
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    {
        std::scoped_lock lock(mutex_);
        if (stream_id != active_stream_id_ || !state_.IsMediaAllowed(call_id)) {
            return;
        }
        endpoint = endpoint_;
    }
    if (endpoint) {
        endpoint->ReceivePcm(samples, sample_count, sample_rate, channels);
    }
}

bool VoiceCallPlugin::SetWebRtcVoiceAuthorization(
    const std::string& stream_id, const std::string& call_id,
    bool authorized) {
    bool applied = false;
    for (const auto& [id, plugin] : GetNetPlugins()) {
        if (plugin && id == kNetRtcLocalPluginId) {
            applied = plugin->SetVoiceCallAuthorization(
                stream_id, call_id, authorized) || applied;
        }
    }
    return applied;
}

void VoiceCallPlugin::SendWebRtcVoicePcm(
    const std::string& stream_id, const std::string& call_id,
    const int16_t* samples, size_t sample_count) {
    {
        std::scoped_lock lock(mutex_);
        if (stream_id != active_stream_id_ || !state_.IsMediaAllowed(call_id)) {
            return;
        }
    }
    for (const auto& [id, plugin] : GetNetPlugins()) {
        if (plugin && id == kNetRtcLocalPluginId) {
            plugin->OnVoiceCallPcm(
                stream_id, call_id, samples, sample_count,
                VoiceAudioEndpoint::kSampleRate, VoiceAudioEndpoint::kChannels);
        }
    }
}

void VoiceCallPlugin::ProcessAudioFrame(const std::shared_ptr<Message>& msg) {
    const auto& frame = msg->voice_audio_frame();
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    {
        std::scoped_lock lock(mutex_);
        if (!IsAuthenticatedSessionLocked(msg->device_id(), msg->stream_id()) ||
            msg->stream_id() != active_stream_id_ ||
            !state_.AcceptMedia(frame.call_id(), frame.sequence())) {
            return;
        }
        endpoint = endpoint_;
    }
    if (endpoint && !frame.opus().empty()) {
        endpoint->ReceiveOpus(
            frame.sequence(), frame.capture_time_ms(),
            frame.opus().data(), frame.opus().size());
    }
}

void VoiceCallPlugin::SendResponse(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint64_t request_id, bool accepted,
    const std::string& reason) {
    Message message;
    message.set_type(kVoiceCallResponse);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* response = message.mutable_voice_call_response();
    // Never reflect an attacker-controlled oversized identifier back onto the
    // authenticated transport. Valid v1 identifiers are bounded by the state
    // machine and retain exact request/response correlation.
    response->set_call_id(call_id.size() <= VoiceCallState::kMaxCallIdBytes
                              ? call_id : std::string{});
    response->set_request_id(request_id);
    response->set_accepted(accepted);
    response->set_reason(reason);
    if (auto data = ProtoAsData(&message); data) {
        DispatchTargetStreamMessage(stream_id, data, true);
    }
}

void VoiceCallPlugin::SendConfig(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id) {
    Message message;
    message.set_type(kVoiceAudioConfig);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* config = message.mutable_voice_audio_config();
    config->set_call_id(call_id);
    config->set_sample_rate(VoiceAudioEndpoint::kSampleRate);
    config->set_channels(VoiceAudioEndpoint::kChannels);
    config->set_frame_ms(VoiceAudioEndpoint::kFrameMs);
    config->set_bitrate_bps(VoiceAudioEndpoint::kBitrateBps);
    config->set_fec(true);
    config->set_dtx(false);
    if (auto data = ProtoAsData(&message); data) {
        DispatchTargetStreamMessage(stream_id, data, true);
    }
}

void VoiceCallPlugin::QueueAudioFrame(
    const std::string&, const std::string& stream_id,
    const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
    const std::vector<uint8_t>& opus) {
    {
        std::scoped_lock lock(mutex_);
        if (!state_.IsMediaAllowed(call_id) || stream_id != active_stream_id_) {
            return;
        }
    }
    packet_transport_.Enqueue({
        .sequence = sequence,
        .capture_time_ms = capture_time_ms,
        .opus = opus,
    });
}

void VoiceCallPlugin::DispatchAudioFrame(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
    const std::vector<uint8_t>& opus) {
    {
        std::scoped_lock lock(mutex_);
        if (!state_.IsMediaAllowed(call_id) || stream_id != active_stream_id_) {
            return;
        }
    }
    Message message;
    message.set_type(kVoiceAudioFrame);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* frame = message.mutable_voice_audio_frame();
    frame->set_call_id(call_id);
    frame->set_sequence(sequence);
    frame->set_capture_time_ms(capture_time_ms);
    frame->set_opus(opus.data(), opus.size());
    if (auto data = ProtoAsData(&message); data) {
        DispatchTargetStreamMessage(stream_id, data, true);
    }
}

void VoiceCallPlugin::EndCall(
    const std::string& call_id, bool notify_remote, const std::string& reason) {
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    std::string device_id;
    std::string stream_id;
    uint64_t request_id = 0;
    bool pending_consent = false;
    {
        std::scoped_lock lock(mutex_);
        if (state_.Phase() == VoiceCallPhase::kIdle || state_.CallId() != call_id) {
            return;
        }
        pending_consent = state_.Phase() == VoiceCallPhase::kIncomingPending;
        request_id = state_.RequestId();
        decision_cache_.Put(call_id, request_id, false,
                            pending_consent ? reason : "stale_request",
                            MonotonicMillis());
        state_.HangUp(call_id);
        endpoint = std::move(endpoint_);
        device_id = std::move(active_device_id_);
        stream_id = std::move(active_stream_id_);
    }
    SetWebRtcVoiceAuthorization(stream_id, call_id, false);
    packet_transport_.Stop();
    if (endpoint) {
        const auto stats = endpoint->Stats();
        const auto transport_stats = packet_transport_.Stats();
        endpoint->Stop();
        LOGI("[VoiceCall] ended reason={}, tx={}, rx={}, underrun={}, plc={}, "
             "jitter_peak={}, jitter_late={}, jitter_drop={}, apm_fail={}/{}, device_rebuilds={}, transport_drop={}",
             reason, stats.encoded_packets, stats.decoded_packets,
             stats.playout_underruns, stats.plc_packets, stats.jitter_peak_packets,
             stats.jitter_late, stats.jitter_overflow_drops,
             stats.apm_capture_failures, stats.apm_render_failures,
             stats.device_rebuilds, transport_stats.congestion_drops);
    }
    if (pending_consent) {
        CancelConsent(stream_id, call_id, request_id, reason);
    }
    if (notify_remote && !stream_id.empty()) {
        Message message;
        message.set_type(kVoiceCallRequest);
        message.set_device_id(device_id);
        message.set_stream_id(stream_id);
        auto* request = message.mutable_voice_call_request();
        request->set_call_id(call_id);
        request->set_request_id(request_id);
        request->set_connect(false);
        if (auto data = ProtoAsData(&message); data) {
            DispatchTargetStreamMessage(stream_id, data, true);
        }
    }
}

bool VoiceCallPlugin::IsAuthenticatedSessionLocked(
    const std::string& device_id, const std::string& stream_id) const {
    if (device_id.empty() || stream_id.empty()) {
        return false;
    }
    // The connection callback supplies the authenticated visitor device id,
    // while media envelopes carry the controlled/render device id.  The
    // authenticated stream id is the stable binding shared by both paths.
    return connected_clients_.contains(stream_id);
}

}  // namespace px
