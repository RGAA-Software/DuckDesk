#pragma once

#include "px_render/architecture/events/render_event.h"
#include "px_render/network/frontend_admission.h"
#include "px_transport/session_open.h"

namespace px {

// One business binding owns the whole QUIC connection, including every auxiliary stream.
// Destruction releases access, never the Windows account/session or workspace applications.
class IrohFrontend final : public std::enable_shared_from_this<IrohFrontend> {
public:
    IrohFrontend(std::weak_ptr<WsTransport> services, std::shared_ptr<WebSocketFrontendLeaseRenewalCoordinator> leases,
                 std::shared_ptr<transport::Connection> connection, FrontendAdmission admission, std::string binding_id,
                 std::string visitor_device_id, bool rdp, RenderEventCallback events);
    ~IrohFrontend();
    [[nodiscard]] static PxAwaitable<PxResult<std::shared_ptr<IrohFrontend>>> AdmitAsync(
        std::weak_ptr<WsTransport> services, std::shared_ptr<WebSocketFrontendLeaseRenewalCoordinator> leases,
        std::shared_ptr<transport::Connection> connection, transport::FrontendParameters parameters, bool rdp, RenderEventCallback events);
    [[nodiscard]] bool Activate();
    void Close(ResourceChannelCloseOutcome outcome = ResourceChannelCloseOutcome::kPeerClosed);
    void UpdatePermissions(const std::vector<std::string>& permissions);
    [[nodiscard]] bool Allows(std::string_view capability) const;
    [[nodiscard]] std::vector<transport::ChannelKind> Channels() const;
    [[nodiscard]] const std::string& StreamId() const { return admission_.stream_id_; }
    [[nodiscard]] const std::string& BindingId() const { return binding_id_; }
    [[nodiscard]] const std::string& LogicalSessionId() const { return admission_.logical_session_id_; }
    [[nodiscard]] bool IsRdp() const { return rdp_; }
    [[nodiscard]] bool IsClosed() const { return closed_ || connection_->IsClosed(); }

private:
    void StartLease(const LogicalSessionGrant& grant);
    std::weak_ptr<WsTransport> services_{};
    std::shared_ptr<WebSocketFrontendLeaseRenewalCoordinator> leases_{};
    std::shared_ptr<transport::Connection> connection_{};
    FrontendAdmission admission_{};
    std::string binding_id_{};
    std::string visitor_device_id_{};
    bool rdp_{};
    mutable std::recursive_mutex lifecycle_mutex_{};
    RenderEventCallback events_{};
    std::atomic_bool closed_{};
    bool active_{};
    bool lease_started_{};
    bool owns_connection_{};
    std::int64_t connected_at_ms_{};
};
}  // namespace px
