#include "services/voice_call_service.h"

#include <atomic>
#include <utility>

#include "px_common_new/log.h"
#include "app/app_messages.h"
#include "px_render/architecture/services/voice_call/voice_call_runtime.h"

namespace px::render {
namespace {

RenderError MakeVoiceCallError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "voice_call",
        .operation = std::move(operation),
        .stage = "service",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

TransportKind ToTransportKind(const std::string& connection_type) {
    if (connection_type == "RTC") {
        return TransportKind::kWebRtcLocal;
    }
    if (connection_type == "UDP") {
        return TransportKind::kUdp;
    }
    if (connection_type == "RELAY") {
        return TransportKind::kRelay;
    }
    return TransportKind::kWebSocket;
}

}  // namespace

std::shared_ptr<VoiceCallService> VoiceCallService::Create(
    const bool enabled,
    TaskPoster task_poster,
    ConsentDelivery consent_delivery,
    StreamSender stream_sender,
    RtcAuthorizationSender rtc_authorization_sender,
    RtcPcmSender rtc_pcm_sender) {
    return std::make_shared<VoiceCallService>(
        enabled, std::move(task_poster), std::move(consent_delivery),
        std::move(stream_sender), std::move(rtc_authorization_sender),
        std::move(rtc_pcm_sender));
}

VoiceCallService::VoiceCallService(
    const bool enabled,
    TaskPoster task_poster,
    ConsentDelivery consent_delivery,
    StreamSender stream_sender,
    RtcAuthorizationSender rtc_authorization_sender,
    RtcPcmSender rtc_pcm_sender)
    : task_poster_(std::move(task_poster)),
      consent_delivery_(std::move(consent_delivery)),
      stream_sender_(std::move(stream_sender)),
      rtc_authorization_sender_(std::move(rtc_authorization_sender)),
      rtc_pcm_sender_(std::move(rtc_pcm_sender)),
      enabled_(enabled) {}

VoiceCallService::~VoiceCallService() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration VoiceCallService::MakeRegistration() {
    const std::weak_ptr<VoiceCallService> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kVoiceCallModuleId),
            .name = "Voice Call",
            .author = "GammaRay",
            .description = "Built-in authenticated voice-call service",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kService,
            .default_enabled = enabled_,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeVoiceCallError(
                      "start", "service owner expired")));
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner ? owner->Stop() : ModuleLifecycleResult{};
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            return owner
                ? owner->SetEnabled(enabled)
                : ModuleLifecycleResult(std::unexpected(MakeVoiceCallError(
                      "set_enabled", "service owner expired")));
        },
    };
}

ModuleLifecycleResult VoiceCallService::Start() {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        if (running_) {
            return {};
        }
        running_ = true;
        runtime_ = VoiceCallRuntime::Make(enabled_, task_poster_);
        runtime = runtime_;
    }
    ConfigureRuntimeDelivery(runtime);
    LOGI("event=service.start component=voice_call enabled={} outcome=success",
         enabled_);
    return {};
}

ModuleLifecycleResult VoiceCallService::Stop() {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        if (!running_ && !runtime_) {
            return {};
        }
        running_ = false;
        routes_.clear();
        runtime = std::move(runtime_);
    }
    if (runtime) {
        runtime->Shutdown("service_stopping");
    }
    LOGI("event=service.stop component=voice_call outcome=success");
    return {};
}

ModuleLifecycleResult VoiceCallService::SetEnabled(const bool enabled) {
    bool restart = false;
    {
        std::lock_guard lock(mutex_);
        restart = running_ && enabled_ != enabled;
        enabled_ = enabled;
    }
    if (restart) {
        static_cast<void>(Stop());
        return Start();
    }
    return {};
}

void VoiceCallService::ConfigureRuntimeDelivery(
    const std::shared_ptr<VoiceCallRuntime>& runtime) {
    if (!runtime) {
        return;
    }
    const std::weak_ptr<VoiceCallService> weak_owner = weak_from_this();
    runtime->SetEventDelivery(
        [weak_owner](const VoiceCallRuntimeEvent& event) {
            if (const auto owner = weak_owner.lock()) {
                owner->HandleRuntimeEvent(event);
            }
        });
}

void VoiceCallService::HandleRuntimeEvent(
    const VoiceCallRuntimeEvent& event) {
    if (event.kind == VoiceCallRuntimeEventKind::kConsent) {
        const VoiceCallConsentNotice notice{
            .show = event.show,
            .visitor_device_id = event.visitor_device_id,
            .stream_id = event.stream_id,
            .call_id = event.call_id,
            .request_id = event.request_id,
            .expires_at_unix_ms = event.expires_at_unix_ms,
            .reason = event.reason,
        };
        const bool delivered = consent_delivery_ && consent_delivery_(notice);
        {
            std::lock_guard lock(mutex_);
            ++consent_notices_;
            if (!delivered) {
                ++rejected_outputs_;
            }
        }
        if (!delivered && notice.show) {
            MsgVoiceCallConsentDecision decision;
            decision.stream_id_ = notice.stream_id;
            decision.call_id_ = notice.call_id;
            decision.request_id_ = notice.request_id;
            decision.accepted_ = false;
            decision.reason_ = "panel_unavailable";
            HandleConsentDecision(decision);
        }
        return;
    }
    if (event.stream_id.empty()) {
        return;
    }
    const auto route = RouteForStream(event.stream_id);
    bool sent = false;
    if (event.kind == VoiceCallRuntimeEventKind::kStreamMessage) {
        sent = event.message && stream_sender_ &&
            stream_sender_(route, event.message);
    }
    else if (event.kind == VoiceCallRuntimeEventKind::kRtcAuthorization) {
        sent = rtc_authorization_sender_ &&
            rtc_authorization_sender_(
                route, event.call_id, event.authorized);
        if (event.authorization_applied) {
            event.authorization_applied->store(sent, std::memory_order_release);
        }
    }
    else if (event.kind == VoiceCallRuntimeEventKind::kRtcPcm &&
             event.pcm && !event.pcm->empty()) {
        sent = rtc_pcm_sender_ && rtc_pcm_sender_(
            route, event.call_id, event.pcm,
            event.sample_rate, event.channels);
    }
    std::lock_guard lock(mutex_);
    ++media_messages_;
    if (!sent) {
        ++rejected_outputs_;
    }
}

void VoiceCallService::On1Second() {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        runtime = runtime_;
    }
    if (runtime) {
        runtime->On1Second();
    }
}

void VoiceCallService::HandleMessage(const std::shared_ptr<Message>& message) {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        runtime = runtime_;
        if (runtime) {
            ++inbound_messages_;
        }
    }
    if (runtime) {
        runtime->OnMessage(message);
    }
}

void VoiceCallService::HandleConsentDecision(
    const MsgVoiceCallConsentDecision& decision) {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        runtime = runtime_;
    }
    if (runtime) {
        runtime->ApplyConsentDecision(VoiceCallConsentDecision{
            .stream_id = decision.stream_id_,
            .call_id = decision.call_id_,
            .request_id = decision.request_id_,
            .accepted = decision.accepted_,
            .reason = decision.reason_,
        });
    }
}

void VoiceCallService::HandleClientConnected(
    const std::string& visitor_device_id,
    const std::string& stream_id,
    const std::string& connection_type,
    const std::string& transport_id) {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        routes_[stream_id] = TransportRoute{
            .kind = ToTransportKind(connection_type),
            .channel = TransportChannelKind::kVoice,
            .transport_id = transport_id,
            .stream_id = stream_id,
        };
        runtime = runtime_;
    }
    if (runtime) {
        runtime->OnClientConnected(visitor_device_id, stream_id, connection_type);
    }
}

void VoiceCallService::HandleClientDisconnected(const std::string& stream_id) {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        routes_.erase(stream_id);
        runtime = runtime_;
    }
    if (runtime) {
        runtime->OnClientDisconnected(stream_id);
    }
}

void VoiceCallService::HandleWebRtcPcm(
    const std::string& stream_id,
    const std::string& call_id,
    const std::span<const std::int16_t> samples,
    const int sample_rate,
    const int channels) {
    std::shared_ptr<VoiceCallRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        runtime = runtime_;
    }
    if (runtime) {
        runtime->ReceiveWebRtcPcm(
            stream_id, call_id, samples, sample_rate, channels);
    }
}

TransportRoute VoiceCallService::RouteForStream(
    const std::string& stream_id) const {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(stream_id);
    if (route != routes_.end()) {
        return route->second;
    }
    return TransportRoute{
        .channel = TransportChannelKind::kVoice,
        .stream_id = stream_id,
    };
}

VoiceCallServiceSnapshot VoiceCallService::Snapshot() const {
    std::lock_guard lock(mutex_);
    return VoiceCallServiceSnapshot{
        .running = running_,
        .enabled = enabled_,
        .inbound_messages = inbound_messages_,
        .consent_notices = consent_notices_,
        .media_messages = media_messages_,
        .rejected_outputs = rejected_outputs_,
    };
}

}  // namespace px::render
