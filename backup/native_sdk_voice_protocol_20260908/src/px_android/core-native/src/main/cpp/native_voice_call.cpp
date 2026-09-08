#include "native_voice_call.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>

#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_message.pb.h"
#include "px_message/proto_converter.h"
#include "px_voice_call/voice_audio_endpoint.h"

namespace pixels::android {
namespace {

constexpr std::int32_t kVoiceIdle = 0;
constexpr std::int32_t kVoiceRequesting = 1;
constexpr std::int32_t kVoiceConnected = 2;

std::uint64_t NextRequestId() {
    static std::atomic_uint64_t sequence{static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    auto value = ++sequence;
    if (value == 0U) {
        value = ++sequence;
    }
    return value;
}

std::uint64_t MonotonicMillis() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

px::Message MakeRequest(const std::string& device_id, const std::string& stream_id, const std::string& call_id, const std::uint64_t request_id,
                        const bool connect) {
    px::Message message;
    message.set_type(px::kVoiceCallRequest);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& request = *message.mutable_voice_call_request();
    request.set_call_id(call_id);
    request.set_request_id(request_id);
    request.set_connect(connect);
    return message;
}

px::Message MakeConfig(const std::string& device_id, const std::string& stream_id, const std::string& call_id) {
    px::Message message;
    message.set_type(px::kVoiceAudioConfig);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& config = *message.mutable_voice_audio_config();
    config.set_call_id(call_id);
    config.set_sample_rate(px::VoiceAudioEndpoint::kSampleRate);
    config.set_channels(px::VoiceAudioEndpoint::kChannels);
    config.set_frame_ms(px::VoiceAudioEndpoint::kFrameMs);
    config.set_bitrate_bps(px::VoiceAudioEndpoint::kBitrateBps);
    config.set_fec(true);
    config.set_dtx(false);
    return message;
}

px::Message MakeAudioFrame(const std::string& device_id, const std::string& stream_id, const std::string& call_id, const std::uint32_t sequence,
                           const std::uint64_t capture_time_ms, const std::vector<std::uint8_t>& opus) {
    px::Message message;
    message.set_type(px::kVoiceAudioFrame);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& frame = *message.mutable_voice_audio_frame();
    frame.set_call_id(call_id);
    frame.set_sequence(sequence);
    frame.set_capture_time_ms(capture_time_ms);
    frame.set_opus(opus.data(), opus.size());
    return message;
}

std::int32_t PublicPhase(const px::VoiceCallPhase phase) {
    switch (phase) {
    case px::VoiceCallPhase::kOutgoingPending:
        return kVoiceRequesting;
    case px::VoiceCallPhase::kConnected:
        return kVoiceConnected;
    case px::VoiceCallPhase::kIdle:
    case px::VoiceCallPhase::kIncomingPending:
        return kVoiceIdle;
    }
    return kVoiceIdle;
}

} // namespace

std::shared_ptr<NativeVoiceCall> NativeVoiceCall::Create(SendMessage send_message, PostTask post_task, StatusCallback status_callback) {
    if (!send_message || !post_task || !status_callback) {
        return {};
    }
    return std::make_shared<NativeVoiceCall>(std::move(send_message), std::move(post_task), std::move(status_callback));
}

NativeVoiceCall::NativeVoiceCall(SendMessage send_message, PostTask post_task, StatusCallback status_callback)
    : send_message_(std::move(send_message)), post_task_(std::move(post_task)), status_callback_(std::move(status_callback)) {}

NativeVoiceCall::~NativeVoiceCall() {
    Shutdown(false, {}, false);
}

void NativeVoiceCall::SetCapabilities(const bool supported, const bool requires_headset) {
    bool stop_active{};
    {
        std::lock_guard lock(mutex_);
        supported_ = supported;
        requires_headset_ = requires_headset;
        stop_active = !supported && state_.Phase() != px::VoiceCallPhase::kIdle;
    }
    if (stop_active) {
        Stop(true, "unsupported");
    } else {
        PublishStatus();
    }
}

bool NativeVoiceCall::Start(const std::string& device_id, const std::string& stream_id) {
    const auto request_id = NextRequestId();
    const auto call_id = px::MD5::Hex(device_id + stream_id + std::to_string(request_id));
    {
        std::lock_guard lock(mutex_);
        if (!supported_ || device_id.empty() || stream_id.empty() || !state_.BeginOutgoing(call_id, request_id, MonotonicMillis())) {
            return false;
        }
        device_id_ = device_id;
        stream_id_ = stream_id;
        inbound_audio_frames_ = 0;
        rejected_audio_frames_ = 0;
        endpoint_rejected_audio_frames_ = 0;
    }
    auto request = MakeRequest(device_id, stream_id, call_id, request_id, true);
    if (!send_message_(px::ProtoAsData(&request))) {
        Stop(false, "request_encode_failed");
        return false;
    }
    LOGI("[VoiceCall] Android request sent, call={}, request={}", px::VoiceCallLogId(call_id), request_id);
    ScheduleTimeout(call_id, request_id);
    PublishStatus();
    return true;
}

bool NativeVoiceCall::SetMicrophoneMuted(const bool muted) {
    std::shared_ptr<px::VoiceAudioEndpoint> endpoint;
    {
        std::lock_guard lock(mutex_);
        if (state_.Phase() != px::VoiceCallPhase::kConnected || !endpoint_) {
            return false;
        }
        microphone_muted_ = muted;
        endpoint = endpoint_;
    }
    endpoint->SetMicrophoneMuted(muted);
    PublishStatus();
    return true;
}

bool NativeVoiceCall::SetSpeakerMuted(const bool muted) {
    std::shared_ptr<px::VoiceAudioEndpoint> endpoint;
    {
        std::lock_guard lock(mutex_);
        if (state_.Phase() != px::VoiceCallPhase::kConnected || !endpoint_) {
            return false;
        }
        speaker_muted_ = muted;
        endpoint = endpoint_;
    }
    endpoint->SetSpeakerMuted(muted);
    PublishStatus();
    return true;
}

void NativeVoiceCall::HandleMessage(const std::shared_ptr<px::Message>& message) {
    if (!message) {
        return;
    }
    if (message->type() == px::kVoiceCallRequest) {
        const auto& request = message->voice_call_request();
        if (!request.connect()) {
            bool matching{};
            {
                std::lock_guard lock(mutex_);
                matching = request.call_id() == state_.CallId();
            }
            if (matching) {
                Stop(false, "remote_hangup");
            }
            return;
        }
        px::Message response;
        response.set_type(px::kVoiceCallResponse);
        response.set_device_id(message->device_id());
        response.set_stream_id(message->stream_id());
        auto& body = *response.mutable_voice_call_response();
        body.set_call_id(request.call_id());
        body.set_request_id(request.request_id());
        body.set_accepted(false);
        body.set_reason("unsupported_direction");
        static_cast<void>(send_message_(px::ProtoAsData(&response)));
    } else if (message->type() == px::kVoiceCallResponse) {
        HandleResponse(message);
    } else if (message->type() == px::kVoiceAudioConfig) {
        HandleAudioConfig(message);
    } else if (message->type() == px::kVoiceAudioFrame) {
        HandleAudioFrame(message);
    }
}

void NativeVoiceCall::HandleResponse(const std::shared_ptr<px::Message>& message) {
    const auto& response = message->voice_call_response();
    {
        std::lock_guard lock(mutex_);
        if (message->device_id() != device_id_ || message->stream_id() != stream_id_ ||
            !state_.ApplyResponse(response.call_id(), response.request_id(), response.accepted())) {
            return;
        }
    }
    timeout_thread_.request_stop();
    if (!response.accepted()) {
        LOGI("[VoiceCall] Android request declined, call={}, reason={}", px::VoiceCallLogId(response.call_id()),
             response.reason().empty() ? "rejected" : response.reason());
        PublishStatus(response.reason().empty() ? "rejected" : response.reason());
        return;
    }

    const auto endpoint = std::make_shared<px::VoiceAudioEndpoint>();
    const auto weak_self = weak_from_this();
    const auto weak_endpoint = std::weak_ptr<px::VoiceAudioEndpoint>{endpoint};
    if (!packet_transport_.Start([weak_self, call_id = response.call_id()](const px::VoiceTransportPacket& packet) {
            if (const auto self = weak_self.lock()) {
                self->DispatchAudioFrame(call_id, packet.sequence, packet.capture_time_ms, packet.opus);
            }
        })) {
        Stop(true, "transport_unavailable");
        return;
    }
    std::string error;
    if (!endpoint->Start(
            [weak_self, call_id = response.call_id()](const std::uint32_t sequence, const std::uint64_t capture_time_ms,
                                                      const std::vector<std::uint8_t>& opus) {
                if (const auto self = weak_self.lock()) {
                    self->QueueAudioFrame(call_id, sequence, capture_time_ms, opus);
                }
            },
            error,
            [weak_self, weak_endpoint, call_id = response.call_id()](const std::string& reason) {
                if (const auto self = weak_self.lock()) {
                    static_cast<void>(self->post_task_([weak_self, weak_endpoint, call_id, reason] {
                        const auto owner = weak_self.lock();
                        const auto expected_endpoint = weak_endpoint.lock();
                        bool active{};
                        if (owner && expected_endpoint) {
                            std::lock_guard lock(owner->mutex_);
                            active = owner->state_.IsMediaAllowed(call_id) && owner->endpoint_ == expected_endpoint;
                        }
                        if (active) {
                            owner->Stop(true, reason.empty() ? "device_lost" : reason);
                        }
                    }));
                }
            })) {
        packet_transport_.Stop();
        Stop(true, error.empty() ? "no_mic" : error);
        return;
    }
    bool keep_endpoint{};
    {
        std::lock_guard lock(mutex_);
        keep_endpoint = state_.IsMediaAllowed(response.call_id());
        if (keep_endpoint) {
            endpoint_ = endpoint;
            microphone_muted_ = false;
            speaker_muted_ = false;
        }
    }
    if (!keep_endpoint) {
        packet_transport_.Stop();
        endpoint->Stop();
        return;
    }
    auto config = MakeConfig(message->device_id(), message->stream_id(), response.call_id());
    if (!send_message_(px::ProtoAsData(&config))) {
        Stop(true, "config_send_failed");
        return;
    }
    const auto backend = endpoint->BackendInfo();
    LOGI("[VoiceCall] Android media connected, call={}, backend={}", px::VoiceCallLogId(response.call_id()), backend.backend);
    PublishStatus();
}

void NativeVoiceCall::HandleAudioConfig(const std::shared_ptr<px::Message>& message) {
    const auto& config = message->voice_audio_config();
    bool active{};
    {
        std::lock_guard lock(mutex_);
        active = message->device_id() == device_id_ && message->stream_id() == stream_id_ && state_.IsMediaAllowed(config.call_id());
    }
    if (active && (config.sample_rate() != px::VoiceAudioEndpoint::kSampleRate || config.channels() != px::VoiceAudioEndpoint::kChannels ||
                   config.frame_ms() != px::VoiceAudioEndpoint::kFrameMs)) {
        Stop(true, "incompatible_audio_config");
    }
}

void NativeVoiceCall::HandleAudioFrame(const std::shared_ptr<px::Message>& message) {
    const auto& frame = message->voice_audio_frame();
    std::shared_ptr<px::VoiceAudioEndpoint> endpoint;
    {
        std::lock_guard lock(mutex_);
        ++inbound_audio_frames_;
        if (message->device_id() != device_id_ || message->stream_id() != stream_id_ || !state_.AcceptMedia(frame.call_id(), frame.sequence())) {
            ++rejected_audio_frames_;
            return;
        }
        endpoint = endpoint_;
    }
    if (endpoint && !frame.opus().empty()) {
        const bool accepted = endpoint->ReceiveOpus(
            frame.sequence(), frame.capture_time_ms(),
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(frame.opus().data()), // NOLINT(gammaray-raw-pointer-boundary)
                                          frame.opus().size()));
        if (!accepted) {
            std::lock_guard lock(mutex_);
            ++endpoint_rejected_audio_frames_;
        }
    }
}

void NativeVoiceCall::QueueAudioFrame(const std::string& call_id, const std::uint32_t sequence, const std::uint64_t capture_time_ms,
                                      const std::vector<std::uint8_t>& opus) {
    {
        std::lock_guard lock(mutex_);
        if (!state_.IsMediaAllowed(call_id)) {
            return;
        }
    }
    static_cast<void>(packet_transport_.Enqueue({.sequence = sequence, .capture_time_ms = capture_time_ms, .opus = opus}));
}

void NativeVoiceCall::DispatchAudioFrame(const std::string& call_id, const std::uint32_t sequence, const std::uint64_t capture_time_ms,
                                         const std::vector<std::uint8_t>& opus) {
    std::string device_id;
    std::string stream_id;
    {
        std::lock_guard lock(mutex_);
        if (!state_.IsMediaAllowed(call_id)) {
            return;
        }
        device_id = device_id_;
        stream_id = stream_id_;
    }
    auto message = MakeAudioFrame(device_id, stream_id, call_id, sequence, capture_time_ms, opus);
    static_cast<void>(send_message_(px::ProtoAsData(&message)));
}

void NativeVoiceCall::ScheduleTimeout(const std::string& call_id, const std::uint64_t request_id) {
    timeout_thread_.request_stop();
    const auto weak_self = weak_from_this();
    timeout_thread_ = std::jthread([weak_self, call_id, request_id](const std::stop_token token) {
        std::mutex wait_mutex;
        std::condition_variable_any wake;
        std::unique_lock wait_lock(wait_mutex);
        if (wake.wait_for(wait_lock, token, std::chrono::milliseconds(px::VoiceCallState::kRequestTimeoutMs), [] { return false; })) {
            return;
        }
        if (token.stop_requested()) {
            return;
        }
        const auto self = weak_self.lock();
        if (!self) {
            return;
        }
        bool expired{};
        {
            std::lock_guard lock(self->mutex_);
            if (self->state_.CallId() == call_id && self->state_.RequestId() == request_id) {
                expired = self->state_.Expire(MonotonicMillis());
            }
        }
        if (expired) {
            std::string device_id;
            std::string stream_id;
            {
                std::lock_guard lock(self->mutex_);
                device_id = self->device_id_;
                stream_id = self->stream_id_;
            }
            auto cancel = MakeRequest(device_id, stream_id, call_id, request_id, false);
            static_cast<void>(self->send_message_(px::ProtoAsData(&cancel)));
            self->PublishStatus("timeout");
        }
    });
}

void NativeVoiceCall::Stop(const bool notify_remote, std::string reason) {
    Shutdown(notify_remote, std::move(reason), true);
}

void NativeVoiceCall::Shutdown(const bool notify_remote, std::string reason, const bool publish_status) {
    std::shared_ptr<px::VoiceAudioEndpoint> endpoint;
    std::string call_id;
    std::string device_id;
    std::string stream_id;
    std::uint64_t request_id{};
    std::uint64_t inbound_audio_frames{};
    std::uint64_t rejected_audio_frames{};
    std::uint64_t endpoint_rejected_audio_frames{};
    {
        std::lock_guard lock(mutex_);
        const bool already_idle = state_.Phase() == px::VoiceCallPhase::kIdle;
        if (already_idle && (!publish_status || reason.empty())) {
            return;
        }
        if (!already_idle) {
            call_id = state_.CallId();
            request_id = state_.RequestId();
            state_.Reset();
        }
        device_id = device_id_;
        stream_id = stream_id_;
        endpoint = std::move(endpoint_);
        microphone_muted_ = false;
        speaker_muted_ = false;
        inbound_audio_frames = inbound_audio_frames_;
        rejected_audio_frames = rejected_audio_frames_;
        endpoint_rejected_audio_frames = endpoint_rejected_audio_frames_;
    }
    timeout_thread_.request_stop();
    packet_transport_.Stop();
    const auto endpoint_stats = endpoint ? endpoint->Stats() : px::VoiceAudioStats{};
    const auto transport_stats = packet_transport_.Stats();
    if (endpoint) {
        endpoint->Stop();
    }
    if (notify_remote && !call_id.empty()) {
        auto request = MakeRequest(device_id, stream_id, call_id, request_id, false);
        static_cast<void>(send_message_(px::ProtoAsData(&request)));
    }
    if (publish_status) {
        LOGI("[VoiceCall] Android media stopped, call={}, reason={}, captured={}, encoded={}, decoded={}, sent={}, congestion_drops={}, "
             "jitter_late={}, jitter_missing={}, jitter_overflow={}, jitter_peak={}, device_rebuilds={}, inbound={}, state_rejected={}, "
             "endpoint_rejected={}",
             px::VoiceCallLogId(call_id), reason, endpoint_stats.captured_frames, endpoint_stats.encoded_packets, endpoint_stats.decoded_packets,
             transport_stats.sent, transport_stats.congestion_drops, endpoint_stats.jitter_late, endpoint_stats.jitter_missing,
             endpoint_stats.jitter_overflow_drops, endpoint_stats.jitter_peak_packets, endpoint_stats.device_rebuilds, inbound_audio_frames,
             rejected_audio_frames, endpoint_rejected_audio_frames);
        PublishStatus(std::move(reason));
    }
}

void NativeVoiceCall::PublishStatus(std::string reason) {
    NativeVoiceCallStatus status;
    StatusCallback callback;
    {
        std::lock_guard lock(mutex_);
        status = {
            .phase = PublicPhase(state_.Phase()),
            .microphone_muted = microphone_muted_,
            .speaker_muted = speaker_muted_,
            .requires_headset = requires_headset_,
            .reason = std::move(reason),
        };
        callback = status_callback_;
    }
    if (callback) {
        callback(status);
    }
}

} // namespace pixels::android
