#include "voice_call_runtime.h"

#include <chrono>
#include <utility>

#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {
namespace {

uint64_t MonotonicMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

void VoiceCallRuntime::DeliveryChannel::Set(EventDelivery next_delivery) {
    std::scoped_lock lock(mutex);
    if (accepting) {
        delivery = std::move(next_delivery);
    }
}

void VoiceCallRuntime::DeliveryChannel::Clear() {
    std::scoped_lock lock(mutex);
    delivery = {};
}

void VoiceCallRuntime::DeliveryChannel::Disable() {
    std::unique_lock lock(mutex);
    accepting = false;
    delivery = {};
    const auto current = std::this_thread::get_id();
    const auto active = active_threads.find(current);
    const size_t current_thread_deliveries =
        active == active_threads.end() ? 0 : active->second;
    while (in_flight > current_thread_deliveries) {
        condition.wait(lock);
    }
}

bool VoiceCallRuntime::DeliveryChannel::Deliver(
    const std::shared_ptr<PxPluginBaseEvent>& event) {
    EventDelivery active_delivery;
    {
        std::scoped_lock lock(mutex);
        if (!accepting || !delivery) {
            return false;
        }
        active_delivery = delivery;
        ++in_flight;
        ++active_threads[std::this_thread::get_id()];
    }
    try {
        active_delivery(event);
    }
    catch (...) {
        {
            std::scoped_lock lock(mutex);
            --in_flight;
            const auto current = std::this_thread::get_id();
            if (--active_threads[current] == 0) {
                active_threads.erase(current);
            }
        }
        condition.notify_all();
        throw;
    }
    {
        std::scoped_lock lock(mutex);
        --in_flight;
        const auto current = std::this_thread::get_id();
        if (--active_threads[current] == 0) {
            active_threads.erase(current);
        }
    }
    condition.notify_all();
    return true;
}

std::shared_ptr<VoiceCallRuntime> VoiceCallRuntime::Make(
    bool enabled,
    std::weak_ptr<PxPluginContext> work_context,
    EndpointFactory endpoint_factory) {
    if (!endpoint_factory) {
        endpoint_factory = [] {
            return std::make_shared<VoiceAudioEndpoint>();
        };
    }
    return std::make_shared<VoiceCallRuntime>(
        ConstructionToken{}, enabled, std::move(work_context),
        std::move(endpoint_factory), std::make_shared<DeliveryChannel>());
}

VoiceCallRuntime::VoiceCallRuntime(
    ConstructionToken,
    bool enabled,
    std::weak_ptr<PxPluginContext> work_context,
    EndpointFactory endpoint_factory,
    std::shared_ptr<DeliveryChannel> delivery_channel)
    : enabled_(enabled),
      work_context_(std::move(work_context)),
      endpoint_factory_(std::move(endpoint_factory)),
      delivery_channel_(std::move(delivery_channel)) {}

VoiceCallRuntime::~VoiceCallRuntime() { Shutdown("runtime_destroyed"); }

void VoiceCallRuntime::SetEventDelivery(EventDelivery delivery) {
    delivery_channel_->Set(std::move(delivery));
}

void VoiceCallRuntime::ClearEventDelivery() { delivery_channel_->Clear(); }

bool VoiceCallRuntime::IsAccepting() const {
    return accepting_.load(std::memory_order_acquire);
}

void VoiceCallRuntime::On1Second() {
    if (!IsAccepting()) {
        return;
    }
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
        decision_cache_.Put(
            expired_call, request_id, false, "timeout", MonotonicMillis());
        active_device_id_.clear();
        active_stream_id_.clear();
    }
    CancelConsent(stream_id, expired_call, request_id, "timeout");
    SendResponse(
        device_id, stream_id, expired_call, request_id, false, "timeout");
}

void VoiceCallRuntime::OnClientConnected(
    const std::string& visitor_device_id,
    const std::string& stream_id,
    const std::string& connection_type) {
    if (!IsAccepting() || visitor_device_id.empty() || stream_id.empty()) {
        return;
    }
    std::scoped_lock lock(mutex_);
    connected_clients_[stream_id] = visitor_device_id;
    connection_types_[stream_id] = connection_type;
}

void VoiceCallRuntime::OnClientDisconnected(const std::string& stream_id) {
    if (!IsAccepting()) {
        return;
    }
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

void VoiceCallRuntime::OnMessage(const std::shared_ptr<Message>& message) {
    if (!IsAccepting() || !message) {
        return;
    }
    if (message->type() >= kVoiceCallRequest &&
        message->type() <= kVoiceAudioConfig) {
        LOGI("[VoiceCall] envelope received, type={}, device={}, stream={}",
             static_cast<int>(message->type()), message->device_id(),
             message->stream_id());
    }
    if (!enabled_) {
        if (message->type() == kVoiceCallRequest &&
            message->voice_call_request().connect()) {
            bool authenticated = false;
            {
                std::scoped_lock lock(mutex_);
                authenticated = IsAuthenticatedSessionLocked(
                    message->device_id(), message->stream_id());
            }
            if (authenticated) {
                const auto& request = message->voice_call_request();
                SendResponse(
                    message->device_id(), message->stream_id(),
                    request.call_id(), request.request_id(), false,
                    "disabled_by_policy");
            }
        }
        return;
    }
    if (message->type() == kVoiceCallRequest) {
        ProcessRequest(message);
    }
    else if (message->type() == kVoiceAudioFrame) {
        ProcessAudioFrame(message);
    }
    else if (message->type() == kVoiceAudioConfig) {
        const auto& config = message->voice_audio_config();
        bool active_call = false;
        {
            std::scoped_lock lock(mutex_);
            active_call = IsAuthenticatedSessionLocked(
                              message->device_id(), message->stream_id()) &&
                message->stream_id() == active_stream_id_ &&
                state_.IsMediaAllowed(config.call_id());
        }
        if (active_call &&
            (config.sample_rate() != VoiceAudioEndpoint::kSampleRate ||
             config.channels() != VoiceAudioEndpoint::kChannels ||
             config.frame_ms() != VoiceAudioEndpoint::kFrameMs)) {
            LOGW("[VoiceCall] incompatible config dropped, stream={}, call={}",
                 message->stream_id(), VoiceCallLogId(config.call_id()));
        }
    }
}

void VoiceCallRuntime::ProcessRequest(
    const std::shared_ptr<Message>& message) {
    const auto& request = message->voice_call_request();
    if (!request.connect()) {
        {
            std::scoped_lock lock(mutex_);
            if (!IsAuthenticatedSessionLocked(
                    message->device_id(), message->stream_id()) ||
                message->stream_id() != active_stream_id_ ||
                request.call_id() != state_.CallId()) {
                return;
            }
        }
        EndCall(request.call_id(), false, "remote_hangup");
        return;
    }

    IncomingVoiceCallResult result;
    bool has_cached_decision = false;
    bool cached_accepted = false;
    std::string cached_reason;
    std::string visitor_device_id;
    {
        std::scoped_lock lock(mutex_);
        if (!IsAuthenticatedSessionLocked(
                message->device_id(), message->stream_id())) {
            LOGW("[VoiceCall] unauthenticated request dropped, stream={}",
                 message->stream_id());
            return;
        }
        visitor_device_id = connected_clients_[message->stream_id()];
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
            active_device_id_ = message->device_id();
            active_stream_id_ = message->stream_id();
        }
    }
    if (result == IncomingVoiceCallResult::kInvalid) {
        SendResponse(
            message->device_id(), message->stream_id(), request.call_id(),
            request.request_id(), false, "invalid_request");
    }
    else if (result == IncomingVoiceCallResult::kBusy) {
        SendResponse(
            message->device_id(), message->stream_id(), request.call_id(),
            request.request_id(), false, "busy");
    }
    else if (result == IncomingVoiceCallResult::kDuplicate &&
             has_cached_decision) {
        SendResponse(
            message->device_id(), message->stream_id(), request.call_id(),
            request.request_id(), cached_accepted, cached_reason);
    }
    else if (result == IncomingVoiceCallResult::kPending) {
        RequestConsent(
            visitor_device_id, message->stream_id(), request.call_id(),
            request.request_id());
    }
}

void VoiceCallRuntime::RequestConsent(
    const std::string& visitor_device_id,
    const std::string& stream_id,
    const std::string& call_id,
    uint64_t request_id) {
    auto event = std::make_shared<PxPluginVoiceCallConsentEvent>();
    event->plugin_name_ = kVoiceCallPluginId;
    event->show_ = true;
    event->visitor_device_id_ = visitor_device_id;
    event->stream_id_ = stream_id;
    event->call_id_ = call_id;
    event->request_id_ = request_id;
    event->expires_at_unix_ms_ =
        TimeUtil::GetCurrentTimestamp() + VoiceCallState::kRequestTimeoutMs;
    (void)delivery_channel_->Deliver(event);
}

void VoiceCallRuntime::CancelConsent(
    const std::string& stream_id,
    const std::string& call_id,
    uint64_t request_id,
    const std::string& reason) {
    if (stream_id.empty() || call_id.empty() || request_id == 0) {
        return;
    }
    auto event = std::make_shared<PxPluginVoiceCallConsentEvent>();
    event->plugin_name_ = kVoiceCallPluginId;
    event->show_ = false;
    event->stream_id_ = stream_id;
    event->call_id_ = call_id;
    event->request_id_ = request_id;
    event->reason_ = reason;
    (void)delivery_channel_->Deliver(event);
}

void VoiceCallRuntime::ApplyConsentDecision(
    const MsgVoiceCallConsentDecision& decision) {
    if (!IsAccepting() || decision.stream_id_.empty() ||
        decision.call_id_.empty() || decision.request_id_ == 0) {
        return;
    }
    std::string device_id;
    bool rejected = false;
    bool expired = false;
    const std::string reject_reason =
        decision.reason_.empty() ? "rejected" : decision.reason_;
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
            decision_cache_.Put(
                decision.call_id_, decision.request_id_, false, "timeout",
                MonotonicMillis());
            active_device_id_.clear();
            active_stream_id_.clear();
        }
        else if (!decision.accepted_) {
            rejected = state_.RejectIncoming(
                decision.call_id_, decision.request_id_);
            if (rejected) {
                decision_cache_.Put(
                    decision.call_id_, decision.request_id_, false,
                    reject_reason, MonotonicMillis());
                active_device_id_.clear();
                active_stream_id_.clear();
            }
        }
    }
    if (expired || rejected) {
        const auto reason = expired ? std::string("timeout") : reject_reason;
        CancelConsent(
            decision.stream_id_, decision.call_id_, decision.request_id_,
            reason);
        SendResponse(
            device_id, decision.stream_id_, decision.call_id_,
            decision.request_id_, false, reason);
        return;
    }
    if (!decision.accepted_) {
        return;
    }

    bool is_webrtc = false;
    {
        std::scoped_lock lock(mutex_);
        const auto type = connection_types_.find(decision.stream_id_);
        is_webrtc =
            type != connection_types_.end() && type->second == "RTC";
    }
    const auto endpoint = endpoint_factory_();
    if (!endpoint) {
        SendResponse(
            device_id, decision.stream_id_, decision.call_id_,
            decision.request_id_, false, "no_mic");
        return;
    }
    const auto weak_self = weak_from_this();
    const auto weak_endpoint = std::weak_ptr<VoiceAudioEndpoint>(endpoint);
    std::string error;
    const bool started = endpoint->Start(
        [weak_self, stream_id = decision.stream_id_,
         call_id = decision.call_id_, is_webrtc](
            uint32_t sequence,
            uint64_t capture_time_ms,
            const std::vector<uint8_t>& opus) {
            if (!is_webrtc) {
                if (const auto self = weak_self.lock()) {
                    self->QueueAudioFrame(
                        stream_id, call_id, sequence, capture_time_ms, opus);
                }
            }
        },
        &error, // NOLINT(gammaray-raw-pointer-boundary): synchronous error output API
        [weak_self, call_id = decision.call_id_, weak_endpoint](
            const std::string& reason) {
            if (const auto self = weak_self.lock()) {
                self->ScheduleEndpointFailure(
                    call_id, weak_endpoint,
                    reason.empty() ? "device_lost" : reason);
            }
        },
        [weak_self, stream_id = decision.stream_id_,
         call_id = decision.call_id_, is_webrtc](
            std::span<const int16_t> samples) {
            if (is_webrtc && !samples.empty()) {
                if (const auto self = weak_self.lock()) {
                    self->SendWebRtcVoicePcm(
                        stream_id, call_id, samples);
                }
            }
        });
    if (!started) {
        bool no_mic = false;
        {
            std::scoped_lock lock(mutex_);
            no_mic = state_.RejectIncoming(
                decision.call_id_, decision.request_id_);
            if (no_mic) {
                decision_cache_.Put(
                    decision.call_id_, decision.request_id_, false, "no_mic",
                    MonotonicMillis());
                active_device_id_.clear();
                active_stream_id_.clear();
            }
        }
        if (no_mic) {
            LOGE("[VoiceCall] audio endpoint failed after local consent: {}", error);
            CancelConsent(
                decision.stream_id_, decision.call_id_, decision.request_id_,
                "no_mic");
            SendResponse(
                device_id, decision.stream_id_, decision.call_id_,
                decision.request_id_, false, "no_mic");
        }
        return;
    }

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
                accepted = state_.AcceptIncoming(
                    decision.call_id_, decision.request_id_);
            }
            if (accepted) {
                endpoint_ = endpoint;
                decision_cache_.Put(
                    decision.call_id_, decision.request_id_, true, "",
                    MonotonicMillis());
            }
            else if (expired_while_starting) {
                decision_cache_.Put(
                    decision.call_id_, decision.request_id_, false, "timeout",
                    MonotonicMillis());
                active_device_id_.clear();
                active_stream_id_.clear();
            }
        }
    }
    if (!accepted) {
        endpoint->Stop();
        if (expired_while_starting) {
            CancelConsent(
                decision.stream_id_, decision.call_id_, decision.request_id_,
                "timeout");
            SendResponse(
                device_id, decision.stream_id_, decision.call_id_,
                decision.request_id_, false, "timeout");
        }
        return;
    }
    if (is_webrtc && !SetWebRtcVoiceAuthorization(
            decision.stream_id_, decision.call_id_, true)) {
        EndCall(decision.call_id_, false, "webrtc_audio_unavailable");
        SendResponse(
            device_id, decision.stream_id_, decision.call_id_,
            decision.request_id_, false, "webrtc_audio_unavailable");
        return;
    }
    if (!is_webrtc && !packet_transport_.Start(
            [weak_self, device_id, stream_id = decision.stream_id_,
             call_id = decision.call_id_](const VoiceTransportPacket& packet) {
                if (const auto self = weak_self.lock()) {
                    self->DispatchAudioFrame(
                        device_id, stream_id, call_id, packet);
                }
            })) {
        EndCall(decision.call_id_, false, "transport_unavailable");
        SendResponse(
            device_id, decision.stream_id_, decision.call_id_,
            decision.request_id_, false, "transport_unavailable");
        return;
    }
    CancelConsent(
        decision.stream_id_, decision.call_id_, decision.request_id_,
        "accepted");
    SendResponse(
        device_id, decision.stream_id_, decision.call_id_,
        decision.request_id_, true, "");
    SendConfig(device_id, decision.stream_id_, decision.call_id_);
}

void VoiceCallRuntime::ReceiveWebRtcPcm(
    const std::string& stream_id,
    const std::string& call_id,
    std::span<const int16_t> samples,
    int sample_rate,
    int channels) {
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    {
        std::scoped_lock lock(mutex_);
        if (!IsAccepting() || stream_id != active_stream_id_ ||
            !state_.IsMediaAllowed(call_id)) {
            return;
        }
        endpoint = endpoint_;
    }
    if (endpoint && !samples.empty()) {
        endpoint->ReceivePcm(samples, sample_rate, channels);
    }
}

void VoiceCallRuntime::ProcessAudioFrame(
    const std::shared_ptr<Message>& message) {
    const auto& frame = message->voice_audio_frame();
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    {
        std::scoped_lock lock(mutex_);
        if (!IsAuthenticatedSessionLocked(
                message->device_id(), message->stream_id()) ||
            message->stream_id() != active_stream_id_ ||
            !state_.AcceptMedia(frame.call_id(), frame.sequence())) {
            return;
        }
        endpoint = endpoint_;
    }
    if (endpoint && !frame.opus().empty()) {
        endpoint->ReceiveOpus(
            frame.sequence(), frame.capture_time_ms(),
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(frame.opus().data()),
                frame.opus().size())); // NOLINT(gammaray-raw-pointer-boundary): protobuf byte-view boundary
    }
}

void VoiceCallRuntime::SendResponse(
    const std::string& device_id,
    const std::string& stream_id,
    const std::string& call_id,
    uint64_t request_id,
    bool accepted,
    const std::string& reason) {
    Message message;
    message.set_type(kVoiceCallResponse);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& response = *message.mutable_voice_call_response();
    response.set_call_id(
        call_id.size() <= VoiceCallState::kMaxCallIdBytes ? call_id
                                                          : std::string{});
    response.set_request_id(request_id);
    response.set_accepted(accepted);
    response.set_reason(reason);
    SendStreamMessage(stream_id, ProtoAsData(&message)); // NOLINT(gammaray-raw-pointer-boundary): synchronous protobuf conversion
}

void VoiceCallRuntime::SendConfig(
    const std::string& device_id,
    const std::string& stream_id,
    const std::string& call_id) {
    Message message;
    message.set_type(kVoiceAudioConfig);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& config = *message.mutable_voice_audio_config();
    config.set_call_id(call_id);
    config.set_sample_rate(VoiceAudioEndpoint::kSampleRate);
    config.set_channels(VoiceAudioEndpoint::kChannels);
    config.set_frame_ms(VoiceAudioEndpoint::kFrameMs);
    config.set_bitrate_bps(VoiceAudioEndpoint::kBitrateBps);
    config.set_fec(true);
    config.set_dtx(false);
    SendStreamMessage(stream_id, ProtoAsData(&message)); // NOLINT(gammaray-raw-pointer-boundary): synchronous protobuf conversion
}

void VoiceCallRuntime::QueueAudioFrame(
    const std::string& stream_id,
    const std::string& call_id,
    uint32_t sequence,
    uint64_t capture_time_ms,
    const std::vector<uint8_t>& opus) {
    {
        std::scoped_lock lock(mutex_);
        if (!IsAccepting() || !state_.IsMediaAllowed(call_id) ||
            stream_id != active_stream_id_) {
            return;
        }
    }
    packet_transport_.Enqueue({
        .sequence = sequence,
        .capture_time_ms = capture_time_ms,
        .opus = opus,
    });
}

void VoiceCallRuntime::DispatchAudioFrame(
    const std::string& device_id,
    const std::string& stream_id,
    const std::string& call_id,
    const VoiceTransportPacket& packet) {
    {
        std::scoped_lock lock(mutex_);
        if (!IsAccepting() || !state_.IsMediaAllowed(call_id) ||
            stream_id != active_stream_id_) {
            return;
        }
    }
    Message message;
    message.set_type(kVoiceAudioFrame);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& frame = *message.mutable_voice_audio_frame();
    frame.set_call_id(call_id);
    frame.set_sequence(packet.sequence);
    frame.set_capture_time_ms(packet.capture_time_ms);
    frame.set_opus(packet.opus.data(), packet.opus.size());
    SendStreamMessage(stream_id, ProtoAsData(&message)); // NOLINT(gammaray-raw-pointer-boundary): synchronous protobuf conversion
}

void VoiceCallRuntime::SendStreamMessage(
    const std::string& stream_id,
    const std::shared_ptr<Data>& data) {
    if (!data || stream_id.empty()) {
        return;
    }
    auto event = std::make_shared<PxPluginVoiceCallMediaEvent>();
    event->plugin_name_ = kVoiceCallPluginId;
    event->action_ = PxVoiceCallMediaAction::kStreamMessage;
    event->stream_id_ = stream_id;
    event->message_ = data;
    (void)delivery_channel_->Deliver(event);
}

bool VoiceCallRuntime::SetWebRtcVoiceAuthorization(
    const std::string& stream_id,
    const std::string& call_id,
    bool authorized) {
    if (stream_id.empty() || call_id.empty()) {
        return false;
    }
    auto applied = std::make_shared<std::atomic_bool>(false);
    auto event = std::make_shared<PxPluginVoiceCallMediaEvent>();
    event->plugin_name_ = kVoiceCallPluginId;
    event->action_ = PxVoiceCallMediaAction::kRtcAuthorization;
    event->stream_id_ = stream_id;
    event->call_id_ = call_id;
    event->authorized_ = authorized;
    event->authorization_applied_ = applied;
    if (!delivery_channel_->Deliver(event)) {
        return false;
    }
    return applied->load(std::memory_order_acquire);
}

void VoiceCallRuntime::SendWebRtcVoicePcm(
    const std::string& stream_id,
    const std::string& call_id,
    std::span<const int16_t> samples) {
    {
        std::scoped_lock lock(mutex_);
        if (!IsAccepting() || stream_id != active_stream_id_ ||
            !state_.IsMediaAllowed(call_id)) {
            return;
        }
    }
    if (samples.empty()) {
        return;
    }
    auto event = std::make_shared<PxPluginVoiceCallMediaEvent>();
    event->plugin_name_ = kVoiceCallPluginId;
    event->action_ = PxVoiceCallMediaAction::kRtcPcm;
    event->stream_id_ = stream_id;
    event->call_id_ = call_id;
    event->pcm_.assign(samples.begin(), samples.end());
    event->sample_rate_ = VoiceAudioEndpoint::kSampleRate;
    event->channels_ = VoiceAudioEndpoint::kChannels;
    (void)delivery_channel_->Deliver(event);
}

void VoiceCallRuntime::ScheduleEndpointFailure(
    const std::string& call_id,
    const std::weak_ptr<VoiceAudioEndpoint>& expected_endpoint,
    const std::string& reason) {
    const auto context = work_context_.lock();
    if (!context) {
        return;
    }
    const auto weak_self = weak_from_this();
    context->PostWorkTask(
        [weak_self, call_id, expected_endpoint, reason] {
            if (const auto self = weak_self.lock()) {
                self->HandleEndpointFailure(
                    call_id, expected_endpoint, reason);
            }
        });
}

void VoiceCallRuntime::HandleEndpointFailure(
    const std::string& call_id,
    const std::weak_ptr<VoiceAudioEndpoint>& expected_endpoint,
    const std::string& reason) {
    const auto expected = expected_endpoint.lock();
    bool still_active = false;
    {
        std::scoped_lock lock(mutex_);
        still_active = IsAccepting() && expected &&
            state_.IsMediaAllowed(call_id) && endpoint_ == expected;
    }
    if (still_active) {
        EndCall(call_id, true, reason.empty() ? "device_lost" : reason);
    }
}

void VoiceCallRuntime::EndCall(
    const std::string& call_id,
    bool notify_remote,
    const std::string& reason) {
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    std::string device_id;
    std::string stream_id;
    uint64_t request_id = 0;
    bool pending_consent = false;
    bool is_webrtc = false;
    {
        std::scoped_lock lock(mutex_);
        if (state_.Phase() == VoiceCallPhase::kIdle ||
            state_.CallId() != call_id) {
            return;
        }
        pending_consent =
            state_.Phase() == VoiceCallPhase::kIncomingPending;
        request_id = state_.RequestId();
        decision_cache_.Put(
            call_id, request_id, false,
            pending_consent ? reason : "stale_request", MonotonicMillis());
        state_.HangUp(call_id);
        endpoint = std::move(endpoint_);
        device_id = std::move(active_device_id_);
        stream_id = std::move(active_stream_id_);
        const auto connection_type = connection_types_.find(stream_id);
        is_webrtc = connection_type != connection_types_.end() &&
            connection_type->second == "RTC";
    }
    if (is_webrtc) {
        (void)SetWebRtcVoiceAuthorization(stream_id, call_id, false);
    }
    packet_transport_.Stop();
    if (endpoint) {
        const auto stats = endpoint->Stats();
        const auto transport_stats = packet_transport_.Stats();
        endpoint->Stop();
        LOGI("[VoiceCall] ended reason={}, tx_opus={}, rx_opus={}, "
             "rx_pcm_samples={}, underrun={}, plc={}, transport_drop={}",
             reason, stats.encoded_packets, stats.decoded_packets,
             stats.received_pcm_samples, stats.playout_underruns,
             stats.plc_packets, transport_stats.congestion_drops);
    }
    if (pending_consent) {
        CancelConsent(stream_id, call_id, request_id, reason);
    }
    if (notify_remote && !stream_id.empty()) {
        Message message;
        message.set_type(kVoiceCallRequest);
        message.set_device_id(device_id);
        message.set_stream_id(stream_id);
        auto& request = *message.mutable_voice_call_request();
        request.set_call_id(call_id);
        request.set_request_id(request_id);
        request.set_connect(false);
        SendStreamMessage(stream_id, ProtoAsData(&message)); // NOLINT(gammaray-raw-pointer-boundary): synchronous protobuf conversion
    }
}

void VoiceCallRuntime::Shutdown(const std::string& reason) {
    if (!accepting_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    std::scoped_lock shutdown_lock(shutdown_mutex_);
    std::string call_id;
    {
        std::scoped_lock lock(mutex_);
        call_id = state_.CallId();
    }
    if (!call_id.empty()) {
        EndCall(call_id, false, reason);
    }
    packet_transport_.Stop();
    {
        std::scoped_lock lock(mutex_);
        connected_clients_.clear();
        connection_types_.clear();
        endpoint_.reset();
    }
    delivery_channel_->Disable();
}

bool VoiceCallRuntime::IsAuthenticatedSessionLocked(
    const std::string& device_id,
    const std::string& stream_id) const {
    return !device_id.empty() && !stream_id.empty() &&
        connected_clients_.contains(stream_id);
}

}  // namespace px
