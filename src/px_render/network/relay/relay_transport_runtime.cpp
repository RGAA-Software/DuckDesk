#include "relay_transport_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

#include "px_common/async_delay.h"
#include "px_common/async_runtime.h"
#include "px_common/client_id_extractor.h"
#include "px_common/console_frontend_relay_credential.h"
#include "px_common/data.h"
#include "px_common/hardware.h"
#include "px_common/ip_util.h"
#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/privacy_log.h"
#include "px_common/secret_buffer.h"
#include "px_common/time_util.h"
#include "px_common/uuid.h"
#include "px_common/ws_control_signal.h"
#include "px_message.pb.h"
#include "px_relay_client/relay_connected_info.h"
#include "px_relay_client/relay_room.h"
#include "px_relay_client/relay_server_sdk.h"
#include "px_relay_client/relay_server_sdk_param.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/architecture/runtime/render_execution_context.h"
#include "px_render/modules/module_ids.h"
#include "relay_message.pb.h"
#include "relay_resource_channel.h"

using namespace px_relay;

namespace px {

namespace {

bool HasRelayPermission(const std::vector<std::string>& permissions, const std::string_view permission) {
    return std::any_of(permissions.begin(), permissions.end(), [permission](const std::string& candidate) { return candidate == permission; });
}

bool VerifyRelayDeviceCredential(const RenderModuleSettings& settings, const std::string& password_hash) {
    if (settings.device_safety_password.empty() && settings.device_random_password.empty()) {
        return true;
    }
    if (password_hash.empty()) {
        return false;
    }
    return (!settings.device_safety_password.empty() && settings.device_safety_password == password_hash) ||
           (!settings.device_random_password.empty() && MD5::Hex(settings.device_random_password) == password_hash);
}

std::int64_t CurrentSystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::chrono::milliseconds FrontendRenewalDelay(const std::uint32_t valid_for_ms) {
    return std::clamp(std::chrono::milliseconds(valid_for_ms / 3U), std::chrono::milliseconds(1000), std::chrono::milliseconds(10000));
}

bool HasSameFrontendIdentity(const ConsoleFrontendGrant& expected, const ConsoleFrontendGrant& renewed) {
    return renewed.valid_for_ms > 0 && renewed.session_id == expected.session_id && renewed.revision == expected.revision &&
           renewed.target_kind == expected.target_kind && renewed.device_id == expected.device_id &&
           renewed.application_id == expected.application_id && renewed.instance_id == expected.instance_id &&
           renewed.client_type == expected.client_type && renewed.access_role == expected.access_role;
}

void DispatchCloseLogicalSessionBinding(const RenderEventCallback& dispatcher, const std::string& logical_session_id, const std::string& binding_id) {
    if (!dispatcher || logical_session_id.empty() || binding_id.empty()) {
        return;
    }
    const auto close = std::make_shared<CloseLogicalSessionBindingEvent>();
    close->logical_session_id_ = logical_session_id;
    close->binding_id_ = binding_id;
    dispatcher(RenderEventEnvelope{.source_id = kRelayTransportId, .payload = close});
}

bool IsRelayPayloadAuthorized(const std::shared_ptr<Data>& payload, const std::vector<std::string>& permissions) {
    if (!payload) {
        return false;
    }
    px::Message message;
    if (!message.ParsePartialFromArray(payload->Bytes().data(), payload->Size())) {
        return false;
    }
    switch (message.type()) {
        case MessageType::kKeyEvent:
        case MessageType::kMouseEvent:
        case MessageType::kGamepadState:
        case MessageType::kReqCtrlAltDelete:
        case MessageType::kTextInput:
            return HasRelayPermission(permissions, "input");
        case MessageType::kClipboardInfo:
        case MessageType::kClipboardInfoResp:
        case MessageType::kClipboardReqAtBegin:
        case MessageType::kClipboardReqBuffer:
        case MessageType::kClipboardReqAtEnd:
        case MessageType::kClipboardRespBuffer:
            return HasRelayPermission(permissions, "clipboard");
        case MessageType::kFileAction:
        case MessageType::kFileResponse:
            return HasRelayPermission(permissions, "file");
        case MessageType::kVoiceCallRequest:
        case MessageType::kVoiceCallResponse:
        case MessageType::kVoiceAudioConfig:
        case MessageType::kVoiceAudioFrame:
            return HasRelayPermission(permissions, "audio");
        default:
            return HasRelayPermission(permissions, "view");
    }
}

}  // namespace

std::shared_ptr<RelayTransportRuntime> RelayTransportRuntime::Create(RelayTransportRuntimeConfig config) {
    return std::make_shared<RelayTransportRuntime>(std::move(config));
}

RelayTransportRuntime::RelayTransportRuntime(RelayTransportRuntimeConfig config) : config_(std::move(config)) {
    frontend_authorizer_ = config_.frontend_authorizer;
    logical_lease_renewer_ = config_.logical_lease_renewer;
}

RelayTransportRuntime::~RelayTransportRuntime() { Stop(); }

void RelayTransportRuntime::Start(const std::shared_ptr<RenderExecutionContext>& context, RenderEventCallback event_callback) {
    {
        std::lock_guard lock(sink_mutex_);
        execution_context_ = context;
        event_callback_ = std::move(event_callback);
    }

    if (stopping_.load(std::memory_order_acquire)) {
        return;
    }
    if (config_.console_frontend_admission_required) {
        frontend_scope_ = PxAsyncScope::Create(config_.async_runtime, PxAsyncLane::kControl);
        if (!frontend_scope_) {
            LOGE("event=module.start component=relay code=ASYNC_SCOPE_UNAVAILABLE operation=frontend_authorization outcome=failed");
            return;
        }
    }
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    const auto config = ConfigSnapshot();
    const auto control = std::make_shared<MonitorControl>();
    const auto weak_self = weak_from_this();
    bool monitor_scheduled{false};
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (!stopping_.load(std::memory_order_acquire)) {
            monitor_control_ = control;
            monitor_scheduled = config.async_runtime && config.async_runtime->DeferBlocking([weak_self, control]() { Monitor(weak_self, control); });
            if (!monitor_scheduled) {
                monitor_control_.reset();
            }
        }
    }
    if (!monitor_scheduled) {
        started_.store(false, std::memory_order_release);
        if (!stopping_.load(std::memory_order_acquire)) {
            LOGE("event=module.start component=relay code=ASYNC_RUNTIME_UNAVAILABLE operation=start_monitor outcome=failed recoverable=false");
        }
    }
}

void RelayTransportRuntime::Stop() {
    if (stopping_.exchange(true)) {
        return;
    }

    CancelAllFrontendLeases();
    const auto frontend_scope = std::exchange(frontend_scope_, {});
    if (frontend_scope) {
        frontend_scope->BeginStop();
        if (!frontend_scope->IsScopeThread() && !frontend_scope->WaitFor(std::chrono::seconds(5))) {
            LOGE("event=async.scope_drain component=relay code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=frontend_authorization outcome=timeout");
        }
    }

    std::shared_ptr<MonitorControl> control;
    {
        std::lock_guard lock(lifecycle_mutex_);
        control = monitor_control_;
    }
    if (control) {
        std::unique_lock lock(control->mutex);
        control->stop_requested = true;
        control->wake_requested = true;
        const auto called_from_monitor = control->worker_thread_id == std::this_thread::get_id();
        lock.unlock();
        control->wake_condition.notify_all();
        if (!called_from_monitor) {
            lock.lock();
            control->stopped_condition.wait(lock, [control]() { return control->completed; });
        }
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        if (monitor_control_ == control) {
            monitor_control_.reset();
        }
    }

    ReleaseConnections(ResourceChannelCloseOutcome::kUserStopped);
    {
        std::lock_guard lock(sink_mutex_);
        event_callback_ = {};
        execution_context_.reset();
    }
}

void RelayTransportRuntime::ConfigureFrontendAuthorizer(RelayFrontendAuthorizer authorizer) {
    std::scoped_lock lock(frontend_services_mutex_);
    frontend_authorizer_ = std::move(authorizer);
}

void RelayTransportRuntime::ConfigureLogicalLeaseRenewer(RelayLogicalLeaseRenewer renewer) {
    std::scoped_lock lock(frontend_services_mutex_);
    logical_lease_renewer_ = std::move(renewer);
}

void RelayTransportRuntime::WakeMonitor() {
    std::shared_ptr<MonitorControl> control;
    {
        std::lock_guard lock(lifecycle_mutex_);
        control = monitor_control_;
    }
    if (!control) {
        return;
    }
    {
        std::lock_guard lock(control->mutex);
        control->wake_requested = true;
    }
    control->wake_condition.notify_all();
}

void RelayTransportRuntime::UpdateSettings(const RenderModuleSettings& settings) {
    bool connection_changed = false;
    {
        std::lock_guard lock(config_mutex_);
        connection_changed = settings.device_id != config_.settings.device_id || settings.relay_host != config_.settings.relay_host ||
                             settings.relay_port != config_.settings.relay_port || settings.appkey != config_.settings.appkey;
        config_.settings = settings;
    }
    if (connection_changed && started_) {
        need_reconnect_ = true;
        LOGW(
            "event=transport.configuration_changed component=relay operation=schedule_reconnect "
            "code=RELAY_CONFIGURATION_CHANGED outcome=pending recoverable=true");
    }
    WakeMonitor();
}

RelayTransportRuntimeConfig RelayTransportRuntime::ConfigSnapshot() const {
    std::lock_guard lock(config_mutex_);
    return config_;
}

PxAwaitable<PxResult<ConsoleFrontendGrant>> RelayTransportRuntime::AuthorizeFrontend(ConsoleFrontendAdmissionRequest request,
                                                                                     const std::chrono::steady_clock::time_point deadline) const {
    RelayFrontendAuthorizer authorizer;
    {
        std::scoped_lock lock(frontend_services_mutex_);
        authorizer = frontend_authorizer_;
    }
    if (!authorizer) {
        std::fill(request.frontend_token.begin(), request.frontend_token.end(), '\0');
        co_return PxResult<ConsoleFrontendGrant>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "relay_frontend_admission", "Console frontend authorizer is unavailable", true));
    }
    co_return co_await authorizer(std::move(request), deadline);
}

bool RelayTransportRuntime::RenewLogicalLease(const LogicalSessionGrant& grant, const std::int64_t now_ms) const {
    RelayLogicalLeaseRenewer renewer;
    {
        std::scoped_lock lock(frontend_services_mutex_);
        renewer = logical_lease_renewer_;
    }
    return renewer && renewer(grant, now_ms);
}

PxAwaitable<void> RelayTransportRuntime::AuthorizeMediaControl(std::weak_ptr<RelayTransportRuntime> weak_runtime,
                                                               std::weak_ptr<RelayServerSdk> weak_server, const std::uint64_t generation,
                                                               std::shared_ptr<RelayMessage> message, std::string visitor_device_id,
                                                               const std::int64_t revision, std::shared_ptr<const SecretBuffer> token) {
    const auto runtime = weak_runtime.lock();
    const auto server = weak_server.lock();
    if (!runtime || !server || !message || !token || !runtime->IsCurrentMediaGeneration(generation)) {
        co_return;
    }
    const auto& request = message->request_control();
    auto admitted = co_await runtime->AuthorizeFrontend(
        ConsoleFrontendAdmissionRequest{
            .request_id = GetUUID(),
            .session_id = request.stream_id(),
            .revision = revision,
            .frontend_token = std::string{token->View()},
        },
        std::chrono::steady_clock::now() + std::chrono::seconds(12));
    if (!runtime->IsCurrentMediaGeneration(generation)) {
        co_return;
    }
    if (!admitted.HasValue()) {
        LOGW("event=session.admit component=relay code={} operation=console_frontend_auth outcome=rejected recoverable={} reason={}",
             admitted.Error().StableCode(), admitted.Error().retryable, admitted.Error().message);
        server->RespondToControl(message, false, "Console frontend authorization was rejected");
        co_return;
    }
    auto grant = admitted.TakeValue();
    const auto settings = runtime->ConfigSnapshot().settings;
    if (grant.target_kind != "cloud_application" || grant.instance_id != settings.device_id || grant.session_id != request.stream_id() ||
        grant.revision != revision || (grant.access_role != "controller" && grant.access_role != "observer") || grant.valid_for_ms == 0) {
        LOGW(
            "event=session.frontend_identity_mismatch component=relay code=CONSOLE_FRONTEND_IDENTITY_MISMATCH "
            "operation=admit_frontend outcome=rejected recoverable=false session={} instance={}",
            PrivacyLogId(request.stream_id()), PrivacyLogId(settings.device_id));
        server->RespondToControl(message, false, "Console frontend identity was rejected");
        co_return;
    }
    const bool controller = grant.access_role == "controller";
    const auto expires_at_ms = CurrentSystemMilliseconds() + static_cast<std::int64_t>(grant.valid_for_ms);
    auto logical_grant = LogicalSessionGrant{
        .logical_session_id = grant.session_id,
        .stream_id = request.stream_id(),
        .subject_id = grant.client_type + ":" + grant.session_id,
        .join_mode = controller ? "control" : "observe",
        .expires_at_ms = expires_at_ms,
        .allow_observer = !controller,
        .allow_takeover = false,
        .input_allowed = controller,
    };
    auto lease = FrontendLeaseRegistration{
        .expected_grant = grant,
        .logical_grant = logical_grant,
        .room_id = request.room_id(),
        .binding_id = "relay:" + request.room_id(),
        .descriptor_revision = revision,
        .token = std::move(token),
    };
    runtime->DispatchMediaAdmission(
        weak_server, generation, message, std::move(visitor_device_id), std::move(logical_grant),
        controller ? std::vector<std::string>{"view", "audio", "input", "clipboard", "file"} : std::vector<std::string>{"view", "audio"},
        std::move(lease));
}

PxAwaitable<void> RelayTransportRuntime::AuthorizeFileTransferControl(std::weak_ptr<RelayTransportRuntime> weak_runtime,
                                                                      std::weak_ptr<RelayServerSdk> weak_server, const std::uint64_t generation,
                                                                      std::shared_ptr<RelayMessage> message, std::string visitor_device_id,
                                                                      const std::int64_t revision, std::shared_ptr<const SecretBuffer> token) {
    const auto runtime = weak_runtime.lock();
    const auto server = weak_server.lock();
    if (!runtime || !server || !message || !token || !runtime->IsCurrentFileTransferGeneration(generation)) {
        co_return;
    }
    const auto& request = message->request_control();
    auto admitted = co_await runtime->AuthorizeFrontend(
        ConsoleFrontendAdmissionRequest{
            .request_id = GetUUID(),
            .session_id = request.stream_id(),
            .revision = revision,
            .frontend_token = std::string{token->View()},
        },
        std::chrono::steady_clock::now() + std::chrono::seconds(12));
    if (!runtime->IsCurrentFileTransferGeneration(generation)) {
        co_return;
    }
    if (!admitted.HasValue()) {
        server->RespondToControl(message, false, "Console frontend authorization was rejected");
        co_return;
    }
    auto grant = admitted.TakeValue();
    const auto settings = runtime->ConfigSnapshot().settings;
    if (grant.target_kind != "cloud_application" || grant.instance_id != settings.device_id || grant.session_id != request.stream_id() ||
        grant.revision != revision || grant.access_role != "controller" || grant.valid_for_ms == 0) {
        server->RespondToControl(message, false, "Console frontend file-transfer identity was rejected");
        co_return;
    }
    const auto expires_at_ms = CurrentSystemMilliseconds() + static_cast<std::int64_t>(grant.valid_for_ms);
    auto logical_grant = LogicalSessionGrant{
        .logical_session_id = grant.session_id,
        .stream_id = request.stream_id(),
        .subject_id = grant.client_type + ":" + grant.session_id,
        .join_mode = "control",
        .expires_at_ms = expires_at_ms,
        .allow_observer = false,
        .allow_takeover = false,
        .input_allowed = true,
    };
    auto lease = FrontendLeaseRegistration{
        .expected_grant = grant,
        .logical_grant = logical_grant,
        .room_id = request.room_id(),
        .binding_id = {},
        .descriptor_revision = revision,
        .token = std::move(token),
        .file_transfer = true,
    };
    runtime->DispatchFileTransferAdmission(weak_server, generation, message, std::move(visitor_device_id), std::move(logical_grant),
                                           std::move(lease));
}

void RelayTransportRuntime::DispatchMediaAdmission(std::weak_ptr<RelayServerSdk> weak_server, const std::uint64_t generation,
                                                   const std::shared_ptr<RelayMessage>& message, std::string visitor_device_id,
                                                   LogicalSessionGrant grant, std::vector<std::string> permissions,
                                                   std::optional<FrontendLeaseRegistration> frontend_lease) {
    if (!message || !message->has_request_control()) {
        return;
    }
    const auto logical_session_id = grant.logical_session_id;
    const auto binding_id = "relay:" + message->request_control().room_id();
    const auto admission = std::make_shared<AdmitLogicalSessionEvent>();
    admission->grant_ = std::move(grant);
    admission->transport_ = LogicalSessionTransport::kRelay;
    admission->binding_id_ = binding_id;
    admission->takeover_ = false;
    RenderEventCallback lifecycle_dispatcher;
    {
        std::lock_guard lock(sink_mutex_);
        lifecycle_dispatcher = event_callback_;
    }
    const auto weak_self = weak_from_this();
    admission->callback_ = [weak_self, weak_server, generation, message, logical_session_id, binding_id,
                            visitor_device_id = std::move(visitor_device_id), permissions = std::move(permissions),
                            frontend_lease = std::move(frontend_lease), lifecycle_dispatcher](const LogicalSessionAdmission& result) mutable {
        const auto owner = weak_self.lock();
        const auto server = weak_server.lock();
        if (!owner || !server || !owner->IsCurrentMediaGeneration(generation)) {
            if (result.code == LogicalSessionAdmissionCode::kAccepted) {
                DispatchCloseLogicalSessionBinding(lifecycle_dispatcher, logical_session_id, binding_id);
            }
            return;
        }
        const auto& request = message->request_control();
        if (result.code != LogicalSessionAdmissionCode::kAccepted) {
            server->RespondToControl(message, false,
                                     result.code == LogicalSessionAdmissionCode::kRemoteAccessDisabled ? std::string{kWsRemoteAccessDisabledSignal}
                                     : result.code == LogicalSessionAdmissionCode::kOccupied
                                         ? "remote controller is occupied; try again in a few seconds"
                                         : "Relay session admission denied");
            return;
        }
        if (!owner->StoreMediaRoute(
                MediaRelayRouteInfo{
                    .room_id = request.room_id(),
                    .stream_id = request.stream_id(),
                    .visitor_device_id = visitor_device_id,
                    .connection_instance_id = binding_id,
                    .logical_session_id = logical_session_id,
                    .permissions = permissions,
                    .created_timestamp = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp()),
                },
                generation)) {
            DispatchCloseLogicalSessionBinding(lifecycle_dispatcher, logical_session_id, binding_id);
            server->RequestStopRelay(request.room_id());
            server->RespondToControl(message, false, "Relay session stopped");
            return;
        }
        if (frontend_lease && !owner->StartFrontendLease(std::move(*frontend_lease))) {
            server->RequestStopRelay(request.room_id());
            owner->CloseMediaRoute(request.room_id(), ResourceChannelCloseOutcome::kIoError);
            server->RespondToControl(message, false, "Relay frontend lease could not start");
            return;
        }
        const auto capabilities = std::make_shared<ApplyLogicalSessionCapabilitiesEvent>();
        capabilities->update_ = PxLogicalSessionCapabilityUpdate{.stream_id_ = request.stream_id(), .permissions_ = permissions};
        owner->Emit(capabilities);
        const auto streaming = std::make_shared<StreamingParametersRequestedEvent>();
        streaming->stream_id_ = request.stream_id();
        streaming->force_gdi_ = request.force_gdi();
        owner->Emit(streaming);
        server->RespondToControl(message, true, "ok");
    };
    Emit(admission);
}

void RelayTransportRuntime::DispatchFileTransferAdmission(std::weak_ptr<RelayServerSdk> weak_server, const std::uint64_t generation,
                                                          const std::shared_ptr<RelayMessage>& message, std::string visitor_device_id,
                                                          LogicalSessionGrant grant, std::optional<FrontendLeaseRegistration> frontend_lease) {
    if (!message || !message->has_request_control()) {
        return;
    }
    const auto room_id = message->request_control().room_id();
    std::string binding_id;
    {
        std::scoped_lock lock(ft_route_mutex_);
        auto [route_iterator, inserted] = ft_routes_.try_emplace(room_id);
        auto& route = route_iterator->second;
        if (inserted || route.connection_instance_id.empty()) {
            route.connection_instance_id = room_id + "#" + std::to_string(++ft_route_generation_);
        }
        route.stream_id = message->request_control().stream_id();
        route.visitor_device_id = std::move(visitor_device_id);
        route.logical_session_id = grant.logical_session_id;
        binding_id = route.connection_instance_id;
    }
    if (frontend_lease) {
        frontend_lease->binding_id = binding_id;
    }
    const auto logical_session_id = grant.logical_session_id;
    const auto admission = std::make_shared<AdmitLogicalSessionEvent>();
    admission->grant_ = std::move(grant);
    admission->transport_ = LogicalSessionTransport::kFileTransfer;
    admission->binding_id_ = binding_id;
    RenderEventCallback lifecycle_dispatcher;
    {
        std::scoped_lock lock(sink_mutex_);
        lifecycle_dispatcher = event_callback_;
    }
    const auto weak_self = weak_from_this();
    admission->callback_ = [weak_self, weak_server, generation, message, logical_session_id, binding_id, frontend_lease = std::move(frontend_lease),
                            lifecycle_dispatcher](const LogicalSessionAdmission& result) mutable {
        const auto runtime = weak_self.lock();
        const auto server = weak_server.lock();
        if (!runtime || !server || !runtime->IsCurrentFileTransferGeneration(generation)) {
            if (result.code == LogicalSessionAdmissionCode::kAccepted) {
                DispatchCloseLogicalSessionBinding(lifecycle_dispatcher, logical_session_id, binding_id);
            }
            return;
        }
        const auto room_id = message->request_control().room_id();
        if (result.code != LogicalSessionAdmissionCode::kAccepted || (frontend_lease && !runtime->StartFrontendLease(std::move(*frontend_lease)))) {
            runtime->CloseFileTransferRoute(room_id, ResourceChannelCloseOutcome::kIoError);
            server->RequestStopRelay(room_id);
            server->RespondToControl(message, false,
                                     result.code == LogicalSessionAdmissionCode::kRemoteAccessDisabled
                                         ? std::string{kWsRemoteAccessDisabledSignal}
                                         : "Relay file-transfer session admission denied");
            return;
        }
        {
            std::scoped_lock lock(runtime->ft_route_mutex_);
            const auto route = runtime->ft_routes_.find(room_id);
            if (route == runtime->ft_routes_.end() || route->second.connection_instance_id != binding_id) {
                DispatchCloseLogicalSessionBinding(lifecycle_dispatcher, logical_session_id, binding_id);
                server->RequestStopRelay(room_id);
                server->RespondToControl(message, false, "Relay file-transfer route stopped");
                return;
            }
            route->second.authorized = true;
        }
        runtime->OpenFileTransferResourceChannel(room_id);
        server->RespondToControl(message, true, "ok");
    };
    Emit(admission);
}

bool RelayTransportRuntime::StartFrontendLease(FrontendLeaseRegistration registration) {
    if (!frontend_scope_ || !frontend_scope_->IsAccepting() || registration.room_id.empty() || !registration.token ||
        registration.expected_grant.valid_for_ms == 0) {
        return false;
    }
    const auto control = std::make_shared<FrontendLeaseControl>();
    {
        std::scoped_lock lock(frontend_leases_mutex_);
        const auto existing = frontend_leases_.find(registration.room_id);
        if (existing != frontend_leases_.end()) {
            existing->second->current.store(false, std::memory_order_release);
        }
        frontend_leases_.insert_or_assign(registration.room_id, control);
    }
    const auto valid_for_ms = registration.expected_grant.valid_for_ms;
    const auto room_id = registration.room_id;
    const auto weak_self = weak_from_this();
    if (!frontend_scope_->Spawn("relay-frontend-lease-renewal", [weak_self, control, registration = std::move(registration), valid_for_ms]() mutable {
            return RunFrontendLease(weak_self, control, std::move(registration), valid_for_ms);
        })) {
        control->current.store(false, std::memory_order_release);
        CancelFrontendLease(room_id);
        return false;
    }
    return true;
}

PxAwaitable<void> RelayTransportRuntime::RunFrontendLease(std::weak_ptr<RelayTransportRuntime> weak_runtime,
                                                          std::shared_ptr<FrontendLeaseControl> control, FrontendLeaseRegistration registration,
                                                          std::uint32_t valid_for_ms) {
    auto lease_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(valid_for_ms);
    auto delay = FrontendRenewalDelay(valid_for_ms);
    for (;;) {
        const auto waited = co_await WaitForAsyncDelay(delay, "relay_frontend_lease_delay");
        const auto runtime = weak_runtime.lock();
        if (!waited || !runtime || !control->current.load(std::memory_order_acquire)) {
            co_return;
        }
        const auto now = std::chrono::steady_clock::now();
        auto renewed = co_await runtime->AuthorizeFrontend(
            ConsoleFrontendAdmissionRequest{
                .request_id = GetUUID(),
                .session_id = registration.expected_grant.session_id,
                .revision = registration.descriptor_revision,
                .frontend_token = std::string{registration.token->View()},
            },
            std::min(now + std::chrono::seconds(12), lease_deadline));
        if (!control->current.load(std::memory_order_acquire)) {
            co_return;
        }
        if (!renewed.HasValue()) {
            const auto retry_time = std::chrono::steady_clock::now();
            if (renewed.Error().retryable && retry_time < lease_deadline) {
                delay = std::min(std::chrono::milliseconds(2000), std::chrono::duration_cast<std::chrono::milliseconds>(lease_deadline - retry_time));
                continue;
            }
            runtime->TerminateFrontendLease(registration, control, renewed.Error().StableCode());
            co_return;
        }
        const auto grant = renewed.TakeValue();
        if (!HasSameFrontendIdentity(registration.expected_grant, grant)) {
            runtime->TerminateFrontendLease(registration, control, "FRONTEND_LEASE_IDENTITY_CHANGED");
            co_return;
        }
        auto renewed_logical_grant = registration.logical_grant;
        const auto now_ms = CurrentSystemMilliseconds();
        renewed_logical_grant.expires_at_ms = now_ms + static_cast<std::int64_t>(grant.valid_for_ms);
        if (!runtime->RenewLogicalLease(renewed_logical_grant, now_ms)) {
            runtime->TerminateFrontendLease(registration, control, "LOGICAL_LEASE_RENEWAL_REJECTED");
            co_return;
        }
        lease_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(grant.valid_for_ms);
        delay = FrontendRenewalDelay(grant.valid_for_ms);
    }
}

void RelayTransportRuntime::TerminateFrontendLease(const FrontendLeaseRegistration& registration,
                                                   const std::shared_ptr<FrontendLeaseControl>& control, const std::string& reason) {
    if (!control->current.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const auto server = registration.file_transfer ? FileTransferSdk() : MediaSdk();
    if (server) {
        server->RequestStopRelay(registration.room_id);
    }
    if (registration.file_transfer) {
        CloseFileTransferRoute(registration.room_id, ResourceChannelCloseOutcome::kPolicyRevoked);
    } else {
        CloseMediaRoute(registration.room_id, ResourceChannelCloseOutcome::kPolicyRevoked);
    }
    LOGW("event=session.lease component=relay operation=renew outcome=revoked code=LOGICAL_LEASE_REVOKED recoverable=false session={} reason={}",
         PrivacyLogId(registration.logical_grant.logical_session_id), reason);
}

void RelayTransportRuntime::CancelFrontendLease(const std::string& room_id) {
    std::shared_ptr<FrontendLeaseControl> control;
    {
        std::scoped_lock lock(frontend_leases_mutex_);
        const auto found = frontend_leases_.find(room_id);
        if (found == frontend_leases_.end()) {
            return;
        }
        control = std::move(found->second);
        frontend_leases_.erase(found);
    }
    control->current.store(false, std::memory_order_release);
}

void RelayTransportRuntime::CancelAllFrontendLeases() {
    std::unordered_map<std::string, std::shared_ptr<FrontendLeaseControl>> controls;
    {
        std::scoped_lock lock(frontend_leases_mutex_);
        controls.swap(frontend_leases_);
    }
    for (const auto& [room_id, control] : controls) {
        static_cast<void>(room_id);
        control->current.store(false, std::memory_order_release);
    }
}

bool RelayTransportRuntime::WaitFor(const std::shared_ptr<MonitorControl>& control, const std::chrono::milliseconds delay) {
    if (!control) {
        return false;
    }
    std::unique_lock lock(control->mutex);
    static_cast<void>(control->wake_condition.wait_for(lock, delay, [control]() { return control->stop_requested || control->wake_requested; }));
    control->wake_requested = false;
    return !control->stop_requested;
}

void RelayTransportRuntime::Monitor(std::weak_ptr<RelayTransportRuntime> runtime, const std::shared_ptr<MonitorControl>& control) {
    {
        std::lock_guard lock(control->mutex);
        control->worker_thread_id = std::this_thread::get_id();
    }
    int connect_count = 0;
    std::vector<RelayDeviceNetInfo> net_info;
    for (const auto& info : IPUtil::ScanIPs()) {
        net_info.push_back(RelayDeviceNetInfo{
            .ip_ = info.ip_addr_,
            .mac_ = info.mac_address_,
        });
    }

    while (true) {
        const auto self = runtime.lock();
        if (!self || self->stopping_.load(std::memory_order_acquire)) {
            break;
        }
        const auto config = self->ConfigSnapshot();
        auto relay_host = config.configured_host;
        auto relay_port = config.configured_port;
        if (!config.settings.relay_host.empty()) {
            relay_host = config.settings.relay_host;
        }
        const auto settings_port = std::atoi(config.settings.relay_port.c_str());
        if (settings_port > 0) {
            relay_port = settings_port;
        }

        if (self->need_reconnect_.exchange(false)) {
            LOGW(
                "event=transport.connection_replaced component=relay operation=apply_configuration "
                "code=RELAY_CONFIGURATION_CHANGED outcome=restarting recoverable=true");
            self->ReleaseConnections(ResourceChannelCloseOutcome::kTransportLost);
            if (!WaitFor(control, std::chrono::milliseconds(500))) {
                break;
            }
        }

        if (config.settings.device_id.empty() || relay_host.empty() || relay_port <= 0 || config.settings.appkey.empty()) {
            if (!WaitFor(control, std::chrono::milliseconds(500))) {
                break;
            }
            continue;
        }

        auto media_sdk = self->MediaSdk();
        if (!media_sdk) {
            self->ConnectMedia(config, relay_host, relay_port, net_info, connect_count++);
        }

        if (!WaitFor(control, std::chrono::seconds(2))) {
            break;
        }

        media_sdk = self->MediaSdk();
        if (media_sdk && media_sdk->IsAlive()) {
            const auto ft_sdk = self->FileTransferSdk();
            if (!ft_sdk) {
                self->ConnectFileTransfer(config, relay_host, relay_port, net_info);
            }
        }
    }
    {
        std::lock_guard lock(control->mutex);
        control->completed = true;
    }
    control->stopped_condition.notify_all();
}

void RelayTransportRuntime::ReleaseConnections(const ResourceChannelCloseOutcome outcome) {
    ++media_generation_;
    ++file_transfer_generation_;
    auto media_sdk = MediaSdk();
    auto ft_sdk = FileTransferSdk();
    SetMediaSdk({});
    SetFileTransferSdk({});
    if (media_sdk) {
        media_sdk->Stop();
    }
    if (ft_sdk) {
        ft_sdk->Stop();
    }
    CloseAllMediaRoutes(outcome);
    CloseAllFileTransferRoutes(outcome);
}

std::shared_ptr<RelayServerSdk> RelayTransportRuntime::MediaSdk() const {
    std::lock_guard lock(sdk_mutex_);
    return relay_media_sdk_;
}

std::shared_ptr<RelayServerSdk> RelayTransportRuntime::FileTransferSdk() const {
    std::lock_guard lock(sdk_mutex_);
    return relay_ft_sdk_;
}

void RelayTransportRuntime::SetMediaSdk(std::shared_ptr<RelayServerSdk> sdk) {
    std::lock_guard lock(sdk_mutex_);
    relay_media_sdk_ = std::move(sdk);
}

void RelayTransportRuntime::SetFileTransferSdk(std::shared_ptr<RelayServerSdk> sdk) {
    std::lock_guard lock(sdk_mutex_);
    relay_ft_sdk_ = std::move(sdk);
}

bool RelayTransportRuntime::IsCurrentMediaGeneration(uint64_t generation) const { return !stopping_ && media_generation_.load() == generation; }

bool RelayTransportRuntime::IsCurrentFileTransferGeneration(uint64_t generation) const {
    return !stopping_ && file_transfer_generation_.load() == generation;
}

void RelayTransportRuntime::ConnectMedia(const RelayTransportRuntimeConfig& config, const std::string& host, int port,
                                         const std::vector<RelayDeviceNetInfo>& net_info, int connect_count) {
    const auto relay_identity = config.relay_device_id.empty() ? config.settings.device_id : config.relay_device_id;
    const auto server_device_id = "server_" + relay_identity;
    LOGI("Connecting relay media channel, attempt: {}, device: {}, host: {}, port: {}", connect_count, server_device_id, host, port);

    const auto sdk = std::make_shared<RelayServerSdk>(RelayServerSdkParam{
        .host_ = host,
        .port_ = port,
        .ssl_ = false,
        .device_id_ = server_device_id,
        .net_info_ = net_info,
        .appkey_ = config.settings.appkey,
        .async_runtime_ = config.async_runtime,
    });
    const auto generation = ++media_generation_;
    SetMediaSdk(sdk);

    const auto weak_self = weak_from_this();
    sdk->SetOnConnectedCallback([]() {});
    sdk->SetOnDisConnectedCallback([]() {});
    sdk->SetOnRelayHelloCallback([weak_self, generation](const std::string& device_id) {
        if (const auto self = weak_self.lock(); self && self->IsCurrentMediaGeneration(generation)) {
            self->ReportRelayAlive(device_id);
        }
    });
    sdk->SetOnRelayHeartbeatCallback([weak_self, generation](const std::string& device_id, int64_t) {
        if (const auto self = weak_self.lock(); self && self->IsCurrentMediaGeneration(generation)) {
            self->ReportRelayAlive(device_id);
        }
    });
    sdk->SetOnPayloadSentCallback([weak_self, generation](const std::vector<std::string>& room_ids, const std::shared_ptr<const Data>& payload) {
        if (const auto self = weak_self.lock(); self && self->IsCurrentMediaGeneration(generation)) {
            self->ReportMediaPayloadSent(room_ids, payload);
        }
    });
    sdk->SetOnRequestControlCallback(
        [weak_self, weak_sdk = std::weak_ptr<RelayServerSdk>(sdk), generation](const std::shared_ptr<RelayMessage>& message) {
            const auto self = weak_self.lock();
            if (!self || !self->IsCurrentMediaGeneration(generation)) {
                return;
            }
            const auto& request = message->request_control();
            LOGI("Relay control request, device: {}, remote: {}, stream: {}, force GDI: {}", request.device_id(), request.remote_device_id(),
                 request.stream_id(), request.force_gdi());
            const auto server = weak_sdk.lock();
            const auto visitor_device_id = ExtractClientId(request.device_id());
            const auto config = self->ConfigSnapshot();
            if (!server || request.stream_id().empty() || request.room_id().empty() || visitor_device_id.empty()) {
                if (server) {
                    server->RespondToControl(message, false, "invalid Relay control request");
                }
                return;
            }
            if (config.console_frontend_admission_required) {
                auto credential = ParseConsoleFrontendRelayCredential(request.safety_pwd_md5());
                message->mutable_request_control()->clear_safety_pwd_md5();
                if (!credential || request.stream_id().empty()) {
                    server->RespondToControl(message, false, "Console frontend credential was rejected");
                    return;
                }
                const auto token = SecretBuffer::Take(std::move(credential->token));
                const auto scope = self->frontend_scope_;
                if (!scope || !scope->Spawn("relay-frontend-admission", [weak_self, weak_sdk, generation, message, visitor_device_id,
                                                                         revision = credential->revision, token]() {
                        return AuthorizeMediaControl(weak_self, weak_sdk, generation, message, visitor_device_id, revision, token);
                    })) {
                    server->RespondToControl(message, false, "Console frontend authorization is unavailable");
                }
                return;
            }
            if (!VerifyRelayDeviceCredential(config.settings, request.safety_pwd_md5())) {
                server->RespondToControl(message, false, "device password was rejected");
                return;
            }
            const auto logical_session_id = "relay-session:" + request.room_id();
            self->DispatchMediaAdmission(weak_sdk, generation, message, visitor_device_id,
                                         LogicalSessionGrant{
                                             .logical_session_id = logical_session_id,
                                             .stream_id = request.stream_id(),
                                             .subject_id = visitor_device_id,
                                             .join_mode = "control",
                                             .expires_at_ms = 0,
                                             .allow_observer = false,
                                             .allow_takeover = false,
                                             .input_allowed = true,
                                         },
                                         {"view", "audio", "input", "clipboard", "file"}, std::nullopt);
        });
    sdk->SetOnRoomPreparedCallback([weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
        const auto self = weak_self.lock();
        if (!self || !self->IsCurrentMediaGeneration(generation)) {
            return;
        }
        const auto prepared = message->room_prepared();
        const auto media_sdk = self->MediaSdk();
        const auto room = media_sdk ? media_sdk->GetRoomById(prepared.room_id()) : std::shared_ptr<RelayRoom>{};
        if (!room) {
            return;
        }
        if (room->creator_stream_id_.empty()) {
            LOGE(
                "event=transport.protocol_error component=relay code=RELAY_CREATOR_STREAM_MISSING "
                "operation=prepare_room outcome=reconnecting recoverable=true");
            self->need_reconnect_ = true;
            self->WakeMonitor();
            return;
        }
        const auto route = self->FindMediaRouteByRoom(prepared.room_id());
        if (route && route->stream_id != room->creator_stream_id_) {
            LOGE(
                "event=transport.protocol_error component=relay code=RELAY_TICKET_STREAM_MISMATCH "
                "operation=prepare_room outcome=disconnect recoverable=false");
            self->need_reconnect_ = true;
            self->WakeMonitor();
            return;
        }
        // A prepared room is already admitted and must be able to receive its
        // first key frame. The peer's explicit resume request can arrive after
        // a static source has emitted its only changed frame, so make readiness
        // authoritative here and request a fresh key frame through the normal
        // resumed-stream event.
        self->paused_stream_.store(false, std::memory_order_release);
        self->Emit(std::make_shared<RelayResumedEvent>());
        const auto activated_route = self->ActivateMediaRoute(prepared.room_id());
        if (activated_route) {
            self->NotifyClientConnected(activated_route->connection_instance_id, activated_route->stream_id, activated_route->visitor_device_id,
                                        activated_route->logical_session_id);
        }
    });
    sdk->SetOnRoomDestroyedCallback([weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
        const auto self = weak_self.lock();
        if (!self || !self->IsCurrentMediaGeneration(generation)) {
            return;
        }
        const auto destroyed = message->room_destroyed();
        const auto media_sdk = self->MediaSdk();
        const auto room = media_sdk ? media_sdk->GetRoomById(destroyed.room_id()) : std::shared_ptr<RelayRoom>{};
        if (!room) {
            LOGE(
                "event=transport.protocol_error component=relay code=RELAY_ROOM_NOT_FOUND operation=destroy_room "
                "outcome=ignored recoverable=true room={}",
                destroyed.room_id());
            return;
        }
        self->CloseMediaRoute(destroyed.room_id(), ResourceChannelCloseOutcome::kPeerClosed);
        if (!media_sdk->HasRelayRooms()) {
            self->paused_stream_ = true;
        }
    });
    sdk->SetOnRequestPauseStreamCallback([weak_self, generation]() {
        if (const auto self = weak_self.lock(); self && self->IsCurrentMediaGeneration(generation)) {
            self->paused_stream_ = true;
            self->Emit(std::make_shared<RelayPausedEvent>());
        }
    });
    sdk->SetOnRequestResumeStreamCallback([weak_self, generation]() {
        if (const auto self = weak_self.lock(); self && self->IsCurrentMediaGeneration(generation)) {
            self->paused_stream_ = false;
            self->Emit(std::make_shared<RelayResumedEvent>());
        }
    });
    sdk->SetOnRelayProtoMessageCallback([weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
        const auto self = weak_self.lock();
        if (!self || !self->IsCurrentMediaGeneration(generation) || message->type() != RelayMessageType::kRelayTargetMessage) {
            return;
        }
        const auto& relay = message->relay();
        const auto room_id = relay.room_ids_size() == 1 ? relay.room_ids(0) : std::string{};
        if (room_id.empty()) {
            LOGW("Drop Relay payload without one unambiguous room");
            return;
        }
        const auto payload = Data::From(relay.payload());
        const auto route = self->FindMediaRouteByRoom(room_id);
        if (!route || !IsRelayPayloadAuthorized(payload, route->permissions)) {
            LOGW("Drop Relay payload denied by the logical-session capability grant");
            return;
        }
        const auto resource_connection_id = self->ResolveMediaResourceConnection(room_id, payload);
        self->EmitNetMessage(payload, TransportChannel::kMedia, route->connection_instance_id, resource_connection_id, false);
    });
    sdk->SetOnNotificationCallback([weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
        if (const auto self = weak_self.lock(); self && self->IsCurrentMediaGeneration(generation)) {
            const auto event = std::make_shared<PanelStreamMessageEvent>();
            event->body_ = Data::From(message->notification().body());
            self->Emit(event);
        }
    });
    sdk->Start();
}

void RelayTransportRuntime::ConnectFileTransfer(const RelayTransportRuntimeConfig& config, const std::string& host, int port,
                                                const std::vector<RelayDeviceNetInfo>& net_info) {
    const auto relay_identity = config.relay_device_id.empty() ? config.settings.device_id : config.relay_device_id;
    const auto device_id = "ft_server_" + relay_identity;
    LOGI("Connecting relay file-transfer channel, device: {}", device_id);
    const auto sdk = std::make_shared<RelayServerSdk>(RelayServerSdkParam{
        .host_ = host,
        .port_ = port,
        .ssl_ = false,
        .device_id_ = device_id,
        .net_info_ = net_info,
        .device_name_ = Hardware::GetDesktopName(),
        .stream_id_ = device_id,
        .appkey_ = config.settings.appkey,
        .async_runtime_ = config.async_runtime,
    });
    const auto generation = ++file_transfer_generation_;
    SetFileTransferSdk(sdk);

    const auto weak_self = weak_from_this();
    sdk->SetOnRelayHelloCallback([weak_self, generation](const std::string& id) {
        if (const auto self = weak_self.lock(); self && self->IsCurrentFileTransferGeneration(generation)) {
            self->ReportRelayAlive(id);
        }
    });
    sdk->SetOnRelayHeartbeatCallback([weak_self, generation](const std::string& id, int64_t) {
        if (const auto self = weak_self.lock(); self && self->IsCurrentFileTransferGeneration(generation)) {
            self->ReportRelayAlive(id);
        }
    });
    sdk->SetOnPayloadSentCallback([weak_self, generation](const std::vector<std::string>& room_ids, const std::shared_ptr<const Data>& payload) {
        if (const auto self = weak_self.lock(); self && self->IsCurrentFileTransferGeneration(generation)) {
            self->ReportFileTransferPayloadSent(room_ids, payload);
        }
    });
    sdk->SetOnRequestControlCallback([weak_self, weak_sdk = std::weak_ptr<RelayServerSdk>{sdk},
                                      generation](const std::shared_ptr<RelayMessage>& message) {
        const auto self = weak_self.lock();
        const auto server = weak_sdk.lock();
        if (!self || !server || !self->IsCurrentFileTransferGeneration(generation) || !message || !message->has_request_control()) {
            return;
        }
        const auto& request = message->request_control();
        const auto visitor_device_id = ExtractClientId(request.device_id().starts_with("ft_") ? request.device_id().substr(3) : request.device_id());
        const auto config = self->ConfigSnapshot();
        if (request.stream_id().empty() || request.room_id().empty() || visitor_device_id.empty()) {
            server->RespondToControl(message, false, "invalid Relay file-transfer request");
            return;
        }
        if (config.console_frontend_admission_required) {
            auto credential = ParseConsoleFrontendRelayCredential(request.safety_pwd_md5());
            message->mutable_request_control()->clear_safety_pwd_md5();
            if (!credential) {
                server->RespondToControl(message, false, "Console frontend credential was rejected");
                return;
            }
            const auto token = SecretBuffer::Take(std::move(credential->token));
            const auto scope = self->frontend_scope_;
            if (!scope || !scope->Spawn("relay-file-transfer-frontend-admission", [weak_self, weak_sdk, generation, message, visitor_device_id,
                                                                                   revision = credential->revision, token]() {
                    return AuthorizeFileTransferControl(weak_self, weak_sdk, generation, message, visitor_device_id, revision, token);
                })) {
                server->RespondToControl(message, false, "Console frontend authorization is unavailable");
            }
            return;
        }
        if (!VerifyRelayDeviceCredential(config.settings, request.safety_pwd_md5())) {
            server->RespondToControl(message, false, "device password was rejected");
            return;
        }
        const auto logical_session_id = "relay-ft-session:" + request.room_id();
        self->DispatchFileTransferAdmission(weak_sdk, generation, message, visitor_device_id,
                                            LogicalSessionGrant{
                                                .logical_session_id = logical_session_id,
                                                .stream_id = request.stream_id(),
                                                .subject_id = visitor_device_id,
                                                .join_mode = "control",
                                                .expires_at_ms = 0,
                                                .allow_observer = false,
                                                .allow_takeover = false,
                                                .input_allowed = false,
                                            },
                                            std::nullopt);
    });
    sdk->SetOnRelayProtoMessageCallback([weak_self, generation](const std::shared_ptr<RelayMessage>& message) {
        const auto self = weak_self.lock();
        if (!self || !self->IsCurrentFileTransferGeneration(generation)) {
            return;
        }
        const auto type = message->type();
        if (type == RelayMessageType::kRelayTargetMessage) {
            const auto& relay = message->relay();
            const auto room_id = relay.room_ids_size() > 0 ? relay.room_ids(0) : std::string{};
            std::string connection_id;
            if (!room_id.empty()) {
                std::lock_guard lock(self->ft_route_mutex_);
                auto [route_it, inserted] = self->ft_routes_.try_emplace(room_id);
                auto& route = route_it->second;
                if (inserted || route.connection_instance_id.empty()) {
                    route.connection_instance_id = room_id + "#" + std::to_string(++self->ft_route_generation_);
                }
                if (!route.authorized) {
                    LOGW("Drop Relay file-transfer payload without an admitted logical session");
                    return;
                }
                if (route.has_recv_msg_index && relay.relay_msg_index() != route.last_recv_msg_index + 1) {
                    LOGE(
                        "event=transport.sequence_gap component=relay_ft code=RELAY_FT_SEQUENCE_GAP operation=receive "
                        "outcome=accepted recoverable=true room={} current={} last={}",
                        room_id, relay.relay_msg_index(), route.last_recv_msg_index);
                }
                route.last_recv_msg_index = relay.relay_msg_index();
                route.has_recv_msg_index = true;
                connection_id = route.connection_instance_id;
            }
            const auto& payload = relay.payload();
            self->EmitNetMessage(Data::From(payload), TransportChannel::kFileTransfer, connection_id, connection_id, true);
        } else if (type == RelayMessageType::kRelayRoomPrepared) {
            const auto& prepared = message->room_prepared();
            std::lock_guard lock(self->ft_route_mutex_);
            auto [route_it, inserted] = self->ft_routes_.try_emplace(prepared.room_id());
            auto& route = route_it->second;
            if (inserted || route.connection_instance_id.empty()) {
                route.connection_instance_id = prepared.room_id() + "#" + std::to_string(++self->ft_route_generation_);
            }
            route.stream_id = prepared.creator_stream_id();
            const auto& creator = prepared.creator_device_id();
            route.visitor_device_id = ExtractClientId(creator.starts_with("ft_") ? creator.substr(3) : creator);
            if (route.created_timestamp == 0) {
                route.created_timestamp = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
            }
        } else if (type == RelayMessageType::kRelayRoomDestroyed) {
            const auto& destroyed = message->room_destroyed();
            FtRelayRouteInfo route;
            {
                std::lock_guard lock(self->ft_route_mutex_);
                const auto current = self->ft_routes_.find(destroyed.room_id());
                if (current != self->ft_routes_.end()) {
                    route = current->second;
                }
            }
            if (!route.connection_instance_id.empty()) {
                self->CloseFileTransferRoute(destroyed.room_id(), ResourceChannelCloseOutcome::kPeerClosed);
            }
        }
    });
    sdk->Start();
}

bool RelayTransportRuntime::StoreMediaRoute(MediaRelayRouteInfo route, const uint64_t generation) {
    if (route.room_id.empty() || route.connection_instance_id.empty()) {
        return false;
    }
    std::lock_guard lock(media_route_mutex_);
    if (stopping_.load(std::memory_order_acquire) || media_generation_.load(std::memory_order_acquire) != generation) {
        return false;
    }
    media_routes_.insert_or_assign(route.room_id, std::move(route));
    return true;
}

std::optional<RelayTransportRuntime::MediaRelayRouteInfo> RelayTransportRuntime::FindMediaRouteByRoom(const std::string& room_id) const {
    std::lock_guard lock(media_route_mutex_);
    const auto route = media_routes_.find(room_id);
    return route == media_routes_.end() ? std::nullopt : std::optional<MediaRelayRouteInfo>{route->second};
}

std::optional<RelayTransportRuntime::MediaRelayRouteInfo> RelayTransportRuntime::FindMediaRouteByConnection(
    const std::string& connection_instance_id) const {
    std::lock_guard lock(media_route_mutex_);
    const auto route = std::find_if(media_routes_.begin(), media_routes_.end(), [&connection_instance_id](const auto& entry) {
        return entry.second.connection_instance_id == connection_instance_id;
    });
    return route == media_routes_.end() ? std::nullopt : std::optional<MediaRelayRouteInfo>{route->second};
}

std::optional<RelayTransportRuntime::MediaRelayRouteInfo> RelayTransportRuntime::ActivateMediaRoute(const std::string& room_id) {
    std::lock_guard lock(media_route_mutex_);
    const auto route = media_routes_.find(room_id);
    if (route == media_routes_.end() || route->second.client_connected) {
        return std::nullopt;
    }
    route->second.client_connected = true;
    return route->second;
}

std::string RelayTransportRuntime::ResolveMediaResourceConnection(const std::string& room_id, const std::shared_ptr<const Data>& payload) {
    const auto channel_kind = RelayResourceChannel::Classify(payload);
    MediaRelayRouteInfo route;
    bool open_channel{false};
    {
        std::lock_guard lock(media_route_mutex_);
        const auto current = media_routes_.find(room_id);
        if (current == media_routes_.end() || !current->second.client_connected) {
            return {};
        }
        route = current->second;
        if (channel_kind == ConsoleResourceChannelKind::kAudio && !current->second.audio_channel_opened) {
            current->second.audio_channel_opened = true;
            open_channel = true;
        } else if (channel_kind == ConsoleResourceChannelKind::kFile && !current->second.file_channel_opened) {
            current->second.file_channel_opened = true;
            open_channel = true;
        }
    }
    const auto resource_connection_id = RelayResourceChannel::ConnectionId(route.connection_instance_id, channel_kind);
    if (open_channel) {
        NotifyResourceChannelOpened(resource_connection_id, route.logical_session_id, channel_kind);
    }
    return resource_connection_id;
}

void RelayTransportRuntime::OpenFileTransferResourceChannel(const std::string& room_id) {
    FtRelayRouteInfo route;
    {
        std::scoped_lock lock(ft_route_mutex_);
        const auto current = ft_routes_.find(room_id);
        if (current == ft_routes_.end() || !current->second.authorized || current->second.resource_channel_opened) {
            return;
        }
        current->second.resource_channel_opened = true;
        route = current->second;
    }
    NotifyResourceChannelOpened(route.connection_instance_id, route.logical_session_id, ConsoleResourceChannelKind::kFile);
}

std::vector<std::string> RelayTransportRuntime::AuthorizedMediaRooms(const std::shared_ptr<Data>& message, const std::string& stream_id) const {
    std::vector<std::string> room_ids;
    const auto sdk = MediaSdk();
    if (!sdk || !message) {
        return room_ids;
    }
    for (const auto& client : sdk->GetConnectedClientInfo()) {
        if (!client || (!stream_id.empty() && client->stream_id_ != stream_id)) {
            continue;
        }
        const auto route = FindMediaRouteByRoom(client->room_id_);
        if (route && IsRelayPayloadAuthorized(message, route->permissions)) {
            room_ids.push_back(client->room_id_);
        }
    }
    return room_ids;
}

void RelayTransportRuntime::CloseMediaRoute(const std::string& room_id, const ResourceChannelCloseOutcome outcome) {
    CancelFrontendLease(room_id);
    MediaRelayRouteInfo route;
    {
        std::lock_guard lock(media_route_mutex_);
        const auto current = media_routes_.find(room_id);
        if (current == media_routes_.end()) {
            return;
        }
        route = current->second;
        media_routes_.erase(current);
    }
    if (route.client_connected) {
        NotifyClientDisconnected(route.connection_instance_id, route.stream_id, route.visitor_device_id, route.created_timestamp,
                                 route.logical_session_id, outcome);
    }
    if (route.audio_channel_opened) {
        NotifyResourceChannelClosed(RelayResourceChannel::ConnectionId(route.connection_instance_id, ConsoleResourceChannelKind::kAudio), outcome);
    }
    if (route.file_channel_opened) {
        NotifyResourceChannelClosed(RelayResourceChannel::ConnectionId(route.connection_instance_id, ConsoleResourceChannelKind::kFile), outcome);
    }
    if (!route.logical_session_id.empty()) {
        const auto close = std::make_shared<CloseLogicalSessionBindingEvent>();
        close->logical_session_id_ = route.logical_session_id;
        close->binding_id_ = route.connection_instance_id;
        Emit(close, true);
    }
}

void RelayTransportRuntime::CloseFileTransferRoute(const std::string& room_id, const ResourceChannelCloseOutcome outcome) {
    CancelFrontendLease(room_id);
    FtRelayRouteInfo route;
    {
        std::scoped_lock lock(ft_route_mutex_);
        const auto current = ft_routes_.find(room_id);
        if (current == ft_routes_.end()) {
            return;
        }
        route = current->second;
        ft_routes_.erase(current);
    }
    if (route.resource_channel_opened) {
        NotifyResourceChannelClosed(route.connection_instance_id, outcome);
    }
    if (route.authorized) {
        NotifyFileTransferRouteDisconnected(route.logical_session_id, route.stream_id, route.connection_instance_id);
    }
    RenderEventCallback dispatcher;
    {
        std::scoped_lock lock(sink_mutex_);
        dispatcher = event_callback_;
    }
    DispatchCloseLogicalSessionBinding(dispatcher, route.logical_session_id, route.connection_instance_id);
}

void RelayTransportRuntime::CloseAllMediaRoutes(const ResourceChannelCloseOutcome outcome) {
    std::vector<std::string> room_ids;
    {
        std::lock_guard lock(media_route_mutex_);
        room_ids.reserve(media_routes_.size());
        for (const auto& [room_id, route] : media_routes_) {
            static_cast<void>(route);
            room_ids.push_back(room_id);
        }
    }
    for (const auto& room_id : room_ids) {
        CloseMediaRoute(room_id, outcome);
    }
}

void RelayTransportRuntime::CloseAllFileTransferRoutes(const ResourceChannelCloseOutcome outcome) {
    std::vector<std::string> room_ids;
    {
        std::lock_guard lock(ft_route_mutex_);
        room_ids.reserve(ft_routes_.size());
        for (const auto& [room_id, route] : ft_routes_) {
            static_cast<void>(route);
            room_ids.push_back(room_id);
        }
    }
    for (const auto& room_id : room_ids) {
        CloseFileTransferRoute(room_id, outcome);
    }
}

void RelayTransportRuntime::Emit(RenderEvent event, const bool directly) {
    const auto is_terminal = std::holds_alternative<std::shared_ptr<CloseLogicalSessionBindingEvent>>(event) ||
                             std::holds_alternative<std::shared_ptr<ClientDisconnectedEvent>>(event) ||
                             std::holds_alternative<std::shared_ptr<ResourceChannelClosedEvent>>(event) ||
                             std::holds_alternative<std::shared_ptr<FileTransferRouteDisconnectedEvent>>(event);
    if (stopping_ && !is_terminal) {
        return;
    }
    RenderEventCallback callback;
    std::shared_ptr<RenderExecutionContext> context;
    {
        std::lock_guard lock(sink_mutex_);
        callback = event_callback_;
        context = execution_context_;
    }
    if (!callback) {
        return;
    }
    const auto envelope = std::make_shared<RenderEventEnvelope>(RenderEventEnvelope{
        .source_id = kRelayTransportId,
        .payload = std::move(event),
    });
    if (directly || !context || (stopping_ && is_terminal)) {
        callback(*envelope);
        return;
    }
    const auto weak_self = weak_from_this();
    static_cast<void>(context->Post([weak_self, envelope, is_terminal]() {
        const auto self = weak_self.lock();
        if (!self || (self->stopping_ && !is_terminal)) {
            return;
        }
        RenderEventCallback queued_callback;
        {
            std::lock_guard lock(self->sink_mutex_);
            queued_callback = self->event_callback_;
        }
        if (queued_callback) {
            queued_callback(*envelope);
        }
    }));
}

void RelayTransportRuntime::EmitNetMessage(std::shared_ptr<Data> message, const TransportChannel& channel, std::string connection_instance_id,
                                           std::string resource_connection_id, const bool directly) {
    const auto event = std::make_shared<NetworkClientEvent>();
    event->is_proto_ = true;
    event->socket_fd_ = 0;
    event->transport_type_ = TransportKind::kWebSocket;
    event->channel_type_ = channel;
    event->message_ = std::move(message);
    event->connection_instance_id_ = std::move(connection_instance_id);
    event->resource_connection_id_ = std::move(resource_connection_id);
    const auto weak_self = weak_from_this();
    event->ack_callback_ = [weak_self](const std::shared_ptr<NetMessageAck>& ack) {
        if (const auto self = weak_self.lock()) {
            self->OnMessageAck(ack);
        }
    };
    Emit(event, directly);
}

void RelayTransportRuntime::NotifyClientConnected(const std::string& connection_id, const std::string& stream_id,
                                                  const std::string& visitor_device_id, const std::string& logical_session_id) {
    const auto event = std::make_shared<ClientConnectedEvent>();
    event->logical_session_id_ = logical_session_id;
    event->connection_id_ = connection_id;
    event->stream_id_ = stream_id;
    event->connection_type_ = "Relay";
    event->visitor_device_id_ = visitor_device_id;
    event->begin_timestamp_ = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
    Emit(event);
}

void RelayTransportRuntime::NotifyClientDisconnected(const std::string& connection_id, const std::string& stream_id,
                                                     const std::string& visitor_device_id, const int64_t begin_timestamp,
                                                     const std::string& logical_session_id, const ResourceChannelCloseOutcome outcome) {
    const auto event = std::make_shared<ClientDisconnectedEvent>();
    event->logical_session_id_ = logical_session_id;
    event->connection_id_ = connection_id;
    event->connection_instance_id_ = connection_id;
    event->stream_id_ = stream_id;
    event->visitor_device_id_ = visitor_device_id;
    event->end_timestamp_ = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
    event->duration_ = event->end_timestamp_ - begin_timestamp;
    event->resource_channel_close_outcome_ = outcome;
    Emit(event);
}

void RelayTransportRuntime::NotifyResourceChannelOpened(const std::string& connection_id, const std::string& logical_session_id,
                                                        const ConsoleResourceChannelKind channel_kind) {
    const auto event = std::make_shared<ResourceChannelOpenedEvent>();
    event->connection_id_ = connection_id;
    event->logical_session_id_ = logical_session_id;
    event->channel_kind_ = channel_kind;
    Emit(event, true);
}

void RelayTransportRuntime::NotifyResourceChannelClosed(const std::string& connection_id, const ResourceChannelCloseOutcome outcome) {
    const auto event = std::make_shared<ResourceChannelClosedEvent>();
    event->connection_id_ = connection_id;
    event->outcome_ = outcome;
    Emit(event, true);
}

void RelayTransportRuntime::NotifyFileTransferRouteDisconnected(const std::string& logical_session_id, const std::string& stream_id,
                                                                const std::string& connection_id) {
    const auto event = std::make_shared<FileTransferRouteDisconnectedEvent>();
    event->logical_session_id_ = logical_session_id;
    event->stream_id_ = stream_id;
    event->connection_id_ = connection_id;
    Emit(event, true);
}

void RelayTransportRuntime::ReportRelayAlive(const std::string& device_id) {
    const auto event = std::make_shared<RelayAliveEvent>();
    event->device_id_ = device_id;
    Emit(event);
}

void RelayTransportRuntime::ReportSentDataSize(std::size_t size) {
    const auto event = std::make_shared<DataSentEvent>();
    event->size_ = size;
    Emit(event);
}

void RelayTransportRuntime::ReportMediaPayloadSent(const std::vector<std::string>& room_ids, const std::shared_ptr<const Data>& payload) {
    if (!payload) {
        return;
    }
    for (const auto& room_id : room_ids) {
        const auto connection_id = ResolveMediaResourceConnection(room_id, payload);
        if (!connection_id.empty()) {
            ReportConnectionTraffic(connection_id, static_cast<std::uint64_t>(payload->Size()), 0);
        }
    }
}

void RelayTransportRuntime::ReportFileTransferPayloadSent(const std::vector<std::string>& room_ids, const std::shared_ptr<const Data>& payload) {
    if (!payload) {
        return;
    }
    std::vector<std::string> connection_ids;
    {
        std::lock_guard lock(ft_route_mutex_);
        connection_ids.reserve(room_ids.size());
        for (const auto& room_id : room_ids) {
            const auto route = ft_routes_.find(room_id);
            if (route != ft_routes_.end() && route->second.resource_channel_opened && !route->second.connection_instance_id.empty()) {
                connection_ids.push_back(route->second.connection_instance_id);
            }
        }
    }
    for (const auto& connection_id : connection_ids) {
        ReportConnectionTraffic(connection_id, static_cast<std::uint64_t>(payload->Size()), 0);
    }
}

void RelayTransportRuntime::ReportConnectionTraffic(const std::string& connection_id, const std::uint64_t sent_bytes,
                                                    const std::uint64_t received_bytes) {
    if (connection_id.empty() || (sent_bytes == 0 && received_bytes == 0)) {
        return;
    }
    const auto event = std::make_shared<ResourceTrafficEvent>();
    event->connection_id_ = connection_id;
    event->sent_bytes_ = sent_bytes;
    event->received_bytes_ = received_bytes;
    Emit(event);
}

void RelayTransportRuntime::PostMedia(std::shared_ptr<Data> message, bool run_through) {
    if (!message || !IsWorking() || (paused_stream_ && !run_through)) {
        return;
    }
    const auto sdk = MediaSdk();
    const auto room_ids = AuthorizedMediaRooms(message);
    if (!sdk || room_ids.empty()) {
        return;
    }
    std::shared_ptr<RenderExecutionContext> context;
    {
        std::lock_guard lock(sink_mutex_);
        context = execution_context_;
    }
    if (context) {
        static_cast<void>(context->Post([sdk, room_ids, message]() { sdk->RelayProtoMessageToRooms(room_ids, message); }));
    }
    ReportSentDataSize(message->Size());
}

bool RelayTransportRuntime::PostTargetMedia(const std::string& stream_id, std::shared_ptr<Data> message, bool run_through) {
    if (!message || !IsWorking()) {
        return false;
    }
    if (paused_stream_ && !run_through) {
        return true;
    }
    const auto sdk = MediaSdk();
    const auto room_ids = AuthorizedMediaRooms(message, stream_id);
    if (!sdk || room_ids.empty()) {
        return false;
    }
    std::shared_ptr<RenderExecutionContext> context;
    {
        std::lock_guard lock(sink_mutex_);
        context = execution_context_;
    }
    if (!context) {
        return false;
    }
    static_cast<void>(context->Post([sdk, room_ids, message]() { sdk->RelayProtoMessageToRooms(room_ids, message); }));
    ReportSentDataSize(message->Size());
    return true;
}

FileTransferSendResult RelayTransportRuntime::PostFileTransfer(const std::string& stream_id, std::shared_ptr<Data> message,
                                                               const std::string& connection_instance_id) {
    if (!message) {
        return FileTransferSendResult::TransportError("relay file-transfer payload is empty");
    }
    if (!IsWorking()) {
        return FileTransferSendResult::Disconnected("relay transport is not working");
    }
    if (const auto media_route = FindMediaRouteByConnection(connection_instance_id)) {
        if (!HasRelayPermission(media_route->permissions, "file")) {
            return FileTransferSendResult::TransportError("Relay logical session does not allow file transfer");
        }
        const auto media_sdk = MediaSdk();
        if (!media_sdk || media_sdk->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
            return media_sdk ? FileTransferSendResult::Busy("relay media queue is full", media_sdk->AcquireFileTransferWritableSignal())
                             : FileTransferSendResult::Disconnected("relay media connection is unavailable");
        }
        media_sdk->RelayProtoMessageToRooms({media_route->room_id}, message);
        ReportSentDataSize(message->Size());
        return FileTransferSendResult::Accepted();
    }
    const auto sdk = FileTransferSdk();
    if (!sdk) {
        return FileTransferSendResult::Disconnected("relay file-transfer connection is unavailable");
    }
    if (sdk->GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
        return FileTransferSendResult::Busy("relay file-transfer queue is full", sdk->AcquireFileTransferWritableSignal());
    }
    sdk->RelayProtoMessage(stream_id, message);
    ReportSentDataSize(message->Size());
    return FileTransferSendResult::Accepted();
}

int RelayTransportRuntime::ConnectedClientsCount() const {
    const auto sdk = MediaSdk();
    return IsWorking() && sdk ? sdk->GetConnectedClientsCount() : 0;
}

bool RelayTransportRuntime::IsWorking() const {
    const auto config = ConfigSnapshot();
    const auto sdk = MediaSdk();
    return !stopping_ && config.settings.relay_enabled && sdk && sdk->IsAlive();
}

int64_t RelayTransportRuntime::QueuingMediaMessageCount() const {
    const auto sdk = MediaSdk();
    return sdk ? sdk->GetQueuingMsgCount() : 0;
}

int64_t RelayTransportRuntime::QueuingFileTransferMessageCount() const {
    const auto sdk = FileTransferSdk();
    return sdk ? sdk->GetQueuingMsgCount() : 0;
}

std::uint64_t RelayTransportRuntime::MediaChannelInstanceGeneration() const { return media_generation_.load(std::memory_order_acquire); }

std::uint64_t RelayTransportRuntime::MediaConnectionAttemptGeneration() const {
    const auto sdk = MediaSdk();
    return sdk ? sdk->ConnectionGeneration() : 0;
}

std::vector<std::shared_ptr<PxConnectedClientInfo>> RelayTransportRuntime::ConnectedClientInfo() const {
    const auto sdk = MediaSdk();
    if (!IsWorking() || !sdk) {
        return {};
    }
    std::vector<std::shared_ptr<PxConnectedClientInfo>> result;
    for (const auto& item : sdk->GetConnectedClientInfo()) {
        result.push_back(std::make_shared<PxConnectedClientInfo>(PxConnectedClientInfo{
            .device_id_ = item->device_id_,
            .stream_id_ = item->stream_id_,
            .relay_room_id_ = item->room_id_,
            .device_name_ = item->device_name_,
        }));
    }
    return result;
}

void RelayTransportRuntime::OnMessageAck(const std::shared_ptr<NetMessageAck>& ack) {
    if (!ack || ack->ch_type_ != TransportChannel::kFileTransfer) {
        return;
    }
    std::lock_guard lock(ack_mutex_);
    if (last_ack_) {
        const auto difference = static_cast<int64_t>(ack->resp_time_) - static_cast<int64_t>(last_ack_->resp_time_);
        LOGI("Relay FT ack interval: {}ms, current: {}, previous: {}", difference, ack->resp_time_, last_ack_->resp_time_);
    }
    last_ack_ = ack;
}

}  // namespace px
