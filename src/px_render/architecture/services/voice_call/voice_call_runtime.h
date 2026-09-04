#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "px_voice_call/voice_audio_endpoint.h"
#include "px_voice_call/voice_call_state.h"
#include "px_voice_call/voice_consent_decision_cache.h"
#include "px_voice_call/voice_packet_transport.h"

namespace px {

class Data;
class Message;

enum class VoiceCallRuntimeEventKind {
    kConsent,
    kStreamMessage,
    kRtcAuthorization,
    kRtcPcm,
};

struct VoiceCallRuntimeEvent final {
    VoiceCallRuntimeEventKind kind{VoiceCallRuntimeEventKind::kConsent};
    bool show{false};
    std::string visitor_device_id;
    std::string stream_id;
    std::string call_id;
    std::uint64_t request_id{0};
    std::uint64_t expires_at_unix_ms{0};
    std::string reason;
    std::shared_ptr<Data> message;
    bool authorized{false};
    std::shared_ptr<std::atomic_bool> authorization_applied;
    std::shared_ptr<const std::vector<std::int16_t>> pcm;
    int sample_rate{0};
    int channels{0};
};

struct VoiceCallConsentDecision final {
    std::string stream_id;
    std::string call_id;
    std::uint64_t request_id{0};
    bool accepted{false};
    std::string reason;
};

class VoiceCallRuntime final : public std::enable_shared_from_this<VoiceCallRuntime> {
public:
    using EventDelivery =
        std::function<void(const VoiceCallRuntimeEvent&)>;
    using EndpointFactory =
        std::function<std::shared_ptr<VoiceAudioEndpoint>()>;
    using TaskPoster = std::function<void(std::function<void()>&&)>;

    static std::shared_ptr<VoiceCallRuntime> Make(
        bool enabled,
        TaskPoster task_poster = {},
        EndpointFactory endpoint_factory = {});
    ~VoiceCallRuntime();

    VoiceCallRuntime(const VoiceCallRuntime&) = delete;
    VoiceCallRuntime& operator=(const VoiceCallRuntime&) = delete;

    void SetEventDelivery(EventDelivery delivery);
    void ClearEventDelivery();
    void On1Second();
    void OnMessage(const std::shared_ptr<Message>& message);
    void ApplyConsentDecision(const VoiceCallConsentDecision& decision);
    void OnClientConnected(
        const std::string& visitor_device_id,
        const std::string& stream_id,
        const std::string& connection_type);
    void OnClientDisconnected(const std::string& stream_id);
    void ReceiveWebRtcPcm(
        const std::string& stream_id,
        const std::string& call_id,
        std::span<const int16_t> samples,
        int sample_rate,
        int channels);
    [[nodiscard]] bool IsAccepting() const;
    void Shutdown(const std::string& reason);

private:
    struct ConstructionToken final {};
    struct DeliveryChannel final {
        void Set(EventDelivery delivery);
        void Clear();
        void Disable();
        [[nodiscard]] bool Deliver(
            const VoiceCallRuntimeEvent& event);

        std::mutex mutex;
        std::condition_variable condition;
        EventDelivery delivery;
        bool accepting = true;
        size_t in_flight = 0;
        std::map<std::thread::id, size_t> active_threads;
    };

public:
    VoiceCallRuntime(
        ConstructionToken,
        bool enabled,
        TaskPoster task_poster,
        EndpointFactory endpoint_factory,
        std::shared_ptr<DeliveryChannel> delivery_channel);

private:
    void ProcessRequest(const std::shared_ptr<Message>& message);
    void ProcessAudioFrame(const std::shared_ptr<Message>& message);
    void RequestConsent(
        const std::string& visitor_device_id,
        const std::string& stream_id,
        const std::string& call_id,
        uint64_t request_id);
    void CancelConsent(
        const std::string& stream_id,
        const std::string& call_id,
        uint64_t request_id,
        const std::string& reason);
    void SendResponse(
        const std::string& device_id,
        const std::string& stream_id,
        const std::string& call_id,
        uint64_t request_id,
        bool accepted,
        const std::string& reason);
    void SendConfig(
        const std::string& device_id,
        const std::string& stream_id,
        const std::string& call_id);
    void QueueAudioFrame(
        const std::string& stream_id,
        const std::string& call_id,
        uint32_t sequence,
        uint64_t capture_time_ms,
        const std::vector<uint8_t>& opus);
    void DispatchAudioFrame(
        const std::string& device_id,
        const std::string& stream_id,
        const std::string& call_id,
        const VoiceTransportPacket& packet);
    void SendStreamMessage(
        const std::string& stream_id,
        const std::shared_ptr<Data>& data);
    [[nodiscard]] bool SetWebRtcVoiceAuthorization(
        const std::string& stream_id,
        const std::string& call_id,
        bool authorized);
    void SendWebRtcVoicePcm(
        const std::string& stream_id,
        const std::string& call_id,
        std::span<const int16_t> samples);
    void ScheduleEndpointFailure(
        const std::string& call_id,
        const std::weak_ptr<VoiceAudioEndpoint>& expected_endpoint,
        const std::string& reason);
    void HandleEndpointFailure(
        const std::string& call_id,
        const std::weak_ptr<VoiceAudioEndpoint>& expected_endpoint,
        const std::string& reason);
    void EndCall(
        const std::string& call_id,
        bool notify_remote,
        const std::string& reason);
    [[nodiscard]] bool IsAuthenticatedSessionLocked(
        const std::string& device_id,
        const std::string& stream_id) const;

    const bool enabled_;
    const TaskPoster task_poster_;
    const EndpointFactory endpoint_factory_;
    const std::shared_ptr<DeliveryChannel> delivery_channel_;
    mutable std::mutex mutex_;
    VoiceCallState state_;
    std::shared_ptr<VoiceAudioEndpoint> endpoint_;
    std::map<std::string, std::string> connected_clients_;
    std::map<std::string, std::string> connection_types_;
    std::string active_device_id_;
    std::string active_stream_id_;
    VoiceConsentDecisionCache decision_cache_;
    VoicePacketTransport packet_transport_;
    std::atomic_bool accepting_ = true;
    std::mutex shutdown_mutex_;
};

}  // namespace px
