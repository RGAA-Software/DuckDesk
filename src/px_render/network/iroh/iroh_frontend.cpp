#include "iroh_frontend.h"

#include <algorithm>
#include <array>

#include "px_common/uuid.h"
#include "px_render/modules/module_ids.h"
#include "px_render/network/ws/ws_transport.h"

namespace px {
namespace {
std::int64_t SystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

PxAsyncError AdmissionError(const std::string& code) {
    return MakePxAsyncError(PxAsyncErrorCode::kServiceRejected, "iroh_frontend_admit", "Frontend admission rejected", false, code);
}

std::string Parameter(const transport::FrontendParameters& parameters, const std::string& name) {
    const auto found = parameters.find(name);
    return found == parameters.end() ? std::string{} : found->second;
}
}  // namespace

IrohFrontend::IrohFrontend(std::weak_ptr<WsTransport> services, std::shared_ptr<WebSocketFrontendLeaseRenewalCoordinator> leases,
                           std::shared_ptr<transport::Connection> connection, FrontendAdmission admission, std::string binding_id,
                           std::string visitor_device_id, bool rdp, RenderEventCallback events)
    : services_(std::move(services)),
      leases_(std::move(leases)),
      connection_(std::move(connection)),
      admission_(std::move(admission)),
      binding_id_(std::move(binding_id)),
      visitor_device_id_(std::move(visitor_device_id)),
      rdp_(rdp),
      events_(std::move(events)) {}

IrohFrontend::~IrohFrontend() { Close(); }

PxAwaitable<PxResult<std::shared_ptr<IrohFrontend>>> IrohFrontend::AdmitAsync(std::weak_ptr<WsTransport> services,
                                                                              std::shared_ptr<WebSocketFrontendLeaseRenewalCoordinator> leases,
                                                                              std::shared_ptr<transport::Connection> connection,
                                                                              transport::FrontendParameters parameters, bool rdp,
                                                                              RenderEventCallback events) {
    if (!connection || !leases || !events || connection->IsClosed())
        co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("TRANSPORT_UNAVAILABLE"));
    const auto peer_identity = connection->PeerId();
    if (!peer_identity) co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("TRANSPORT_UNAVAILABLE"));
    // Endpoint identity comes from the authenticated QUIC peer, never a client-supplied address.
    const bool file_transfer_only = Parameter(parameters, "file_transfer_only") == "1";
    if (file_transfer_only && rdp) co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("SESSION_CAPABILITY_DENIED"));
    auto authentication = co_await AuthenticateFrontendAsync(services, parameters, *peer_identity, !file_transfer_only);
    if (!authentication.HasValue()) co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(authentication.Error());
    const auto frontend = std::make_shared<IrohFrontend>(services, leases, connection, authentication.TakeValue(), "iroh:" + GenerateRandomBase64Id(),
                                                         Parameter(parameters, "visitor_device_id"), rdp, std::move(events));
    auto& admission = frontend->admission_;
    frontend->file_transfer_only_ = file_transfer_only;
    if (file_transfer_only) {
        if (!frontend->Allows("file")) co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("SESSION_CAPABILITY_DENIED"));
        // The caller can reduce an existing grant, never gain additional permissions.
        admission.permissions_ = {"file"};
    } else if (admission.console_frontend_grant_ && admission.console_frontend_grant_->access_role == "file_transfer") {
        co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("SESSION_CAPABILITY_DENIED"));
    }
    if (Parameter(parameters, "stream_id") != admission.stream_id_ || admission.stream_id_.empty())
        co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("SESSION_STREAM_MISMATCH"));
    if ((Parameter(parameters, "rdp") == "1") != rdp)
        co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("SESSION_MODE_MISMATCH"));
    if (rdp) {
        if (admission.join_mode_ != "control" ||
            !std::ranges::all_of(std::array{"rdp", "view", "input", "audio", "clipboard"},
                                 [&frontend](std::string_view capability) { return frontend->Allows(capability); }))
            co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("SESSION_CAPABILITY_DENIED"));
        admission.allow_observer_ = false;
        admission.allow_takeover_ = false;
    }
    const LogicalSessionGrant grant{
        .logical_session_id = admission.logical_session_id_,
        .stream_id = admission.stream_id_,
        .subject_id = admission.subject_id_,
        .join_mode = admission.join_mode_,
        .expires_at_ms = admission.expires_at_ms_,
        .allow_observer = admission.allow_observer_,
        .allow_takeover = admission.allow_takeover_,
        .input_allowed = frontend->Allows("input"),
    };
    const auto logical_transport = file_transfer_only ? LogicalSessionTransport::kFileTransfer : LogicalSessionTransport::kIroh;
    auto bound = co_await AdmitFrontendSessionAsync(services, grant, logical_transport, frontend->binding_id_);
    if (!bound.HasValue()) co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(bound.Error());
    if (bound.Value().code != LogicalSessionAdmissionCode::kAccepted) {
        const auto code = bound.Value().code == LogicalSessionAdmissionCode::kOccupied               ? "SESSION_OCCUPIED"
                          : bound.Value().code == LogicalSessionAdmissionCode::kRemoteAccessDisabled ? "REMOTE_ACCESS_DISABLED"
                                                                                                     : "SESSION_ADMISSION_DENIED";
        co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError(code));
    }
    if (connection->IsClosed()) co_return PxResult<std::shared_ptr<IrohFrontend>>::Failure(AdmissionError("TRANSPORT_CLOSED"));
    frontend->owns_connection_ = true;
    frontend->StartLease(grant);
    co_return PxResult<std::shared_ptr<IrohFrontend>>::Success(frontend);
}

void IrohFrontend::StartLease(const LogicalSessionGrant& grant) {
    lease_started_ = true;
    const auto weak_frontend = weak_from_this();
    const auto terminate = [weak_frontend] {
        if (const auto frontend = weak_frontend.lock()) frontend->Close(ResourceChannelCloseOutcome::kPolicyRevoked);
    };
    if (admission_.console_frontend_grant_ && admission_.frontend_token_) {
        leases_->Start(
            WebSocketFrontendLeaseIdentity{
                .expected_grant = *admission_.console_frontend_grant_,
                .logical_grant = grant,
                .descriptor_session_id = admission_.descriptor_session_id_,
                .descriptor_revision = admission_.descriptor_revision_,
                .binding_id = binding_id_,
                .terminate_transport = terminate,
            },
            admission_.frontend_token_, admission_.console_frontend_grant_->valid_for_ms);
    } else if (!admission_.direct_quota_id_.empty()) {
        leases_->StartDirect(
            WebSocketDirectLeaseIdentity{
                .quota_id = admission_.direct_quota_id_, .logical_grant = grant, .binding_id = binding_id_, .terminate_transport = terminate},
            admission_.direct_valid_for_ms_);
    }
}

void IrohFrontend::UpdatePermissions(const std::vector<std::string>& permissions) {
    std::lock_guard lock(lifecycle_mutex_);
    admission_.permissions_ = file_transfer_only_ ? (std::ranges::find(permissions, "file") != permissions.end() ? std::vector<std::string>{"file"}
                                                                                                                 : std::vector<std::string>{})
                                                  : permissions;
    if (rdp_ && !std::ranges::all_of(std::array{"rdp", "view", "input", "audio", "clipboard"},
                                     [owner = shared_from_this()](std::string_view capability) { return owner->Allows(capability); }))
        Close(ResourceChannelCloseOutcome::kPolicyRevoked);
}

bool IrohFrontend::Allows(std::string_view capability) const {
    std::lock_guard lock(lifecycle_mutex_);
    return std::ranges::find(admission_.permissions_, capability) != admission_.permissions_.end();
}

std::vector<transport::ChannelKind> IrohFrontend::Channels() const {
    std::vector<transport::ChannelKind> channels{transport::ChannelKind::kControl};
    if (rdp_) {
        channels.push_back(transport::ChannelKind::kRdp);
        return channels;
    }
    if (Allows("input")) channels.push_back(transport::ChannelKind::kInput);
    if (Allows("clipboard")) channels.push_back(transport::ChannelKind::kClipboard);
    if (Allows("file")) channels.push_back(transport::ChannelKind::kFile);
    return channels;
}

bool IrohFrontend::Activate() {
    std::lock_guard lock(lifecycle_mutex_);
    if (closed_ || connection_->IsClosed()) return false;
    if (active_) return true;
    const auto services = services_.lock();
    if (!services) return false;
    active_ = true;
    connected_at_ms_ = SystemMilliseconds();
    const auto connected = std::make_shared<ClientConnectedEvent>();
    connected->logical_session_id_ = admission_.logical_session_id_;
    connected->connection_id_ = binding_id_;
    connected->stream_id_ = admission_.stream_id_;
    connected->connection_type_ = "iroh";
    connected->visitor_device_id_ = visitor_device_id_;
    connected->begin_timestamp_ = connected_at_ms_;
    events_(RenderEventEnvelope{.source_id = kNetIrohTransportId, .payload = connected});
    return true;
}

void IrohFrontend::Close(ResourceChannelCloseOutcome outcome) {
    std::lock_guard lock(lifecycle_mutex_);
    if (closed_.exchange(true)) return;
    if (owns_connection_) connection_->Close();
    if (lease_started_) {
        leases_->Cancel(binding_id_);
    } else if (!admission_.direct_quota_id_.empty()) {
        leases_->ReleaseDirectQuota(admission_.direct_quota_id_);
    }
    DispatchCloseLogicalSessionBinding(services_, admission_.logical_session_id_, binding_id_);
    if (!active_) return;
    const auto disconnected = std::make_shared<ClientDisconnectedEvent>();
    disconnected->logical_session_id_ = admission_.logical_session_id_;
    disconnected->connection_id_ = binding_id_;
    disconnected->connection_instance_id_ = binding_id_;
    disconnected->stream_id_ = admission_.stream_id_;
    disconnected->visitor_device_id_ = visitor_device_id_;
    disconnected->end_timestamp_ = SystemMilliseconds();
    disconnected->duration_ = disconnected->end_timestamp_ - connected_at_ms_;
    disconnected->resource_channel_close_outcome_ = outcome;
    events_(RenderEventEnvelope{.source_id = kNetIrohTransportId, .payload = disconnected});
}
}  // namespace px
