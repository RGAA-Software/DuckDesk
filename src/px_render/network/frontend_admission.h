#pragma once

#include "px_common/async_result.h"
#include "px_render/network/ws/frontend_lease_renewal.h"

namespace px {
class WsTransport;
struct FrontendAdmission {
    std::vector<std::string> permissions_{};
    std::string logical_session_id_{};
    std::string stream_id_{};
    std::string join_mode_{};
    std::string subject_id_{};
    int64_t expires_at_ms_ = 0;
    bool allow_observer_ = true;
    bool allow_takeover_ = true;
    std::shared_ptr<WebSocketFrontendToken> frontend_token_{};
    std::optional<ConsoleFrontendGrant> console_frontend_grant_{};
    std::string descriptor_session_id_{};
    std::int64_t descriptor_revision_{};
    std::string direct_quota_id_{};
    std::uint32_t direct_valid_for_ms_{};
};

// Reuses current Console login/resource-session admission; no transport token or additional challenge.
[[nodiscard]] PxAwaitable<PxResult<FrontendAdmission>> AuthenticateFrontendAsync(std::weak_ptr<WsTransport> services,
                                                                                 std::unordered_map<std::string, std::string> parameters,
                                                                                 std::string peer_identity, bool primary_channel);
[[nodiscard]] PxAwaitable<PxResult<LogicalSessionAdmission>> AdmitFrontendSessionAsync(std::weak_ptr<WsTransport> services, LogicalSessionGrant grant,
                                                                                       LogicalSessionTransport transport, std::string binding_id);
void DispatchCloseLogicalSessionBinding(const std::weak_ptr<WsTransport>& services, const std::string& logical_session_id,
                                        const std::string& binding_id);
}  // namespace px
