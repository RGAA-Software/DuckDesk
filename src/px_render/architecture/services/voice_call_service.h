#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "modules/builtin_module_catalog.h"
#include "network/transport_route.h"

namespace px {
class Data;
class Message;
class MsgVoiceCallConsentDecision;
class VoiceCallRuntime;
struct VoiceCallRuntimeEvent;
}

namespace px::render {

inline constexpr std::string_view kVoiceCallModuleId =
    "5a48bb2e-f98b-4d49-a73a-31e49ae45239";

struct VoiceCallConsentNotice final {
    bool show{false};
    std::string visitor_device_id;
    std::string stream_id;
    std::string call_id;
    std::uint64_t request_id{0};
    std::uint64_t expires_at_unix_ms{0};
    std::string reason;
};

struct VoiceCallServiceSnapshot final {
    bool running{false};
    bool enabled{true};
    std::uint64_t inbound_messages{0};
    std::uint64_t consent_notices{0};
    std::uint64_t media_messages{0};
    std::uint64_t rejected_outputs{0};
};

class VoiceCallService final
    : public std::enable_shared_from_this<VoiceCallService> {
public:
    using TaskPoster = std::function<void(std::function<void()>&&)>;
    using ConsentDelivery = std::function<bool(const VoiceCallConsentNotice&)>;
    using StreamSender = std::function<bool(
        const TransportRoute&, const std::shared_ptr<Data>&)>;
    using RtcAuthorizationSender = std::function<bool(
        const TransportRoute&, const std::string&, bool)>;
    using RtcPcmSender = std::function<bool(
        const TransportRoute&, const std::string&,
        const std::shared_ptr<const std::vector<std::int16_t>>&,
        int, int)>;

    [[nodiscard]] static std::shared_ptr<VoiceCallService> Create(
        bool enabled,
        TaskPoster task_poster,
        ConsentDelivery consent_delivery,
        StreamSender stream_sender,
        RtcAuthorizationSender rtc_authorization_sender,
        RtcPcmSender rtc_pcm_sender);

    VoiceCallService(bool enabled,
                     TaskPoster task_poster,
                     ConsentDelivery consent_delivery,
                     StreamSender stream_sender,
                     RtcAuthorizationSender rtc_authorization_sender,
                     RtcPcmSender rtc_pcm_sender);
    ~VoiceCallService();

    VoiceCallService(const VoiceCallService&) = delete;
    VoiceCallService& operator=(const VoiceCallService&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    void On1Second();
    void HandleMessage(const std::shared_ptr<Message>& message);
    void HandleConsentDecision(const MsgVoiceCallConsentDecision& decision);
    void HandleClientConnected(const std::string& visitor_device_id,
                               const std::string& stream_id,
                               const std::string& connection_type,
                               const std::string& transport_id = {});
    void HandleClientDisconnected(const std::string& stream_id);
    void HandleWebRtcPcm(const std::string& stream_id,
                         const std::string& call_id,
                         std::span<const std::int16_t> samples,
                         int sample_rate,
                         int channels);
    [[nodiscard]] VoiceCallServiceSnapshot Snapshot() const;

private:
    void ConfigureRuntimeDelivery(const std::shared_ptr<VoiceCallRuntime>& runtime);
    void HandleRuntimeEvent(const VoiceCallRuntimeEvent& event);
    [[nodiscard]] TransportRoute RouteForStream(
        const std::string& stream_id) const;

    const TaskPoster task_poster_;
    const ConsentDelivery consent_delivery_;
    const StreamSender stream_sender_;
    const RtcAuthorizationSender rtc_authorization_sender_;
    const RtcPcmSender rtc_pcm_sender_;
    mutable std::mutex mutex_;
    bool running_{false};
    bool enabled_{true};
    std::shared_ptr<VoiceCallRuntime> runtime_;
    std::unordered_map<std::string, TransportRoute> routes_;
    std::uint64_t inbound_messages_{0};
    std::uint64_t consent_notices_{0};
    std::uint64_t media_messages_{0};
    std::uint64_t rejected_outputs_{0};
};

}  // namespace px::render
