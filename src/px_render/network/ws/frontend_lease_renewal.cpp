#include "frontend_lease_renewal.h"

#include <algorithm>
#include <charconv>
#include <chrono>

#include "px_common/async_delay.h"
#include "px_common/log.h"
#include "px_common/privacy_log.h"
#include "px_common/uuid.h"
#include "px_render/architecture/events/render_event.h"
#include "ws_transport.h"

namespace px {
namespace {

std::int64_t CurrentSystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::chrono::milliseconds RenewalDelay(const std::uint32_t valid_for_ms) {
    return std::clamp(std::chrono::milliseconds(valid_for_ms / 3), std::chrono::milliseconds(1000), std::chrono::milliseconds(10000));
}

}  // namespace

WebSocketFrontendToken::WebSocketFrontendToken(std::string value) : value_(std::move(value)) {}

WebSocketFrontendToken::~WebSocketFrontendToken() { std::fill(value_.begin(), value_.end(), '\0'); }

std::string WebSocketFrontendToken::Copy() const { return value_; }

std::optional<WebSocketFrontendDescriptor> ConsumeWebSocketFrontendDescriptor(std::unordered_map<std::string, std::string>& query_parameters) {
    const auto session_iterator = query_parameters.find("session_id");
    const auto revision_iterator = query_parameters.find("session_revision");
    auto token_iterator = query_parameters.find("frontend_token");
    const auto stream_iterator = query_parameters.find("stream_id");
    std::int64_t revision{};
    const bool revision_valid = revision_iterator != query_parameters.end() && !revision_iterator->second.empty() && [&] {
        const auto& text = revision_iterator->second;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), revision);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && revision > 0;
    }();
    if (session_iterator == query_parameters.end() || session_iterator->second.empty() || !revision_valid ||
        token_iterator == query_parameters.end() || token_iterator->second.empty() || stream_iterator == query_parameters.end() ||
        stream_iterator->second != session_iterator->second) {
        return std::nullopt;
    }
    auto descriptor = WebSocketFrontendDescriptor{
        .session_id = session_iterator->second,
        .revision = revision,
        .stream_id = stream_iterator->second,
        .token = std::make_shared<WebSocketFrontendToken>(std::move(token_iterator->second)),
    };
    query_parameters.erase(token_iterator);
    return descriptor;
}

bool IsAcceptedWebSocketFrontendGrant(const WebSocketFrontendDescriptor& descriptor, const std::string& render_instance_id,
                                      const ConsoleFrontendGrant& grant) {
    return grant.target_kind == "cloud_application" && grant.instance_id == render_instance_id &&
           (grant.access_role == "controller" || grant.access_role == "observer") && grant.session_id == descriptor.session_id &&
           grant.revision == descriptor.revision && grant.valid_for_ms > 0;
}

bool HasSameConsoleFrontendIdentity(const ConsoleFrontendGrant& expected, const ConsoleFrontendGrant& renewed) {
    return renewed.valid_for_ms > 0 && renewed.session_id == expected.session_id && renewed.revision == expected.revision &&
           renewed.target_kind == expected.target_kind && renewed.device_id == expected.device_id &&
           renewed.application_id == expected.application_id && renewed.instance_id == expected.instance_id &&
           renewed.client_type == expected.client_type && renewed.access_role == expected.access_role;
}

WebSocketFrontendLeaseRenewalCoordinator::WebSocketFrontendLeaseRenewalCoordinator(std::weak_ptr<WsTransport> transport,
                                                                                   std::shared_ptr<PxAsyncScope> async_scope)
    : transport_(std::move(transport)), async_scope_(std::move(async_scope)) {}

void WebSocketFrontendLeaseRenewalCoordinator::Start(WebSocketFrontendLeaseIdentity identity, std::shared_ptr<WebSocketFrontendToken> token,
                                                     const std::uint32_t initial_valid_for_ms) {
    if (!token || identity.binding_id.empty() || !async_scope_ || !async_scope_->IsAccepting() || initial_valid_for_ms == 0) {
        return;
    }
    const auto control = std::make_shared<RenewalControl>();
    {
        std::scoped_lock lock(controls_mutex_);
        const auto existing = controls_.find(identity.binding_id);
        if (existing != controls_.end()) {
            existing->second->current.store(false, std::memory_order_release);
        }
        controls_.insert_or_assign(identity.binding_id, control);
    }
    const auto weak_owner = weak_from_this();
    const bool spawned = async_scope_->Spawn("console-frontend-lease-renewal", [weak_owner, control, identity = std::move(identity),
                                                                                token = std::move(token), initial_valid_for_ms]() mutable {
        return Run(std::move(weak_owner), std::move(control), std::move(identity), std::move(token), initial_valid_for_ms);
    });
    if (!spawned) {
        control->current.store(false, std::memory_order_release);
        RemoveCurrent(control);
    }
}

void WebSocketFrontendLeaseRenewalCoordinator::Cancel(const std::string& binding_id) {
    std::shared_ptr<RenewalControl> control{};
    {
        std::scoped_lock lock(controls_mutex_);
        const auto found = controls_.find(binding_id);
        if (found == controls_.end()) {
            return;
        }
        control = std::move(found->second);
        controls_.erase(found);
    }
    control->current.store(false, std::memory_order_release);
}

PxAwaitable<void> WebSocketFrontendLeaseRenewalCoordinator::Run(std::weak_ptr<WebSocketFrontendLeaseRenewalCoordinator> weak_owner,
                                                                std::shared_ptr<RenewalControl> control, WebSocketFrontendLeaseIdentity identity,
                                                                std::shared_ptr<WebSocketFrontendToken> token, std::uint32_t valid_for_ms) {
    auto lease_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(valid_for_ms);
    auto delay = RenewalDelay(valid_for_ms);
    for (;;) {
        const auto waited = co_await WaitForAsyncDelay(delay, "frontend_lease_delay");
        const auto owner = weak_owner.lock();
        if (!waited || !owner || !control->current.load(std::memory_order_acquire)) {
            co_return;
        }
        const auto transport = owner->transport_.lock();
        if (!transport) {
            co_return;
        }
        const auto request_started_at = std::chrono::steady_clock::now();
        const auto request_deadline = std::min(request_started_at + std::chrono::seconds(12), lease_deadline);
        auto renewed = co_await transport->AdmitFrontend(ConsoleFrontendAdmissionRequest{.request_id = GetUUID(),
                                                                                         .session_id = identity.descriptor_session_id,
                                                                                         .revision = identity.descriptor_revision,
                                                                                         .frontend_token = token->Copy()},
                                                         request_deadline);
        if (!control->current.load(std::memory_order_acquire)) {
            co_return;
        }
        if (!renewed.HasValue()) {
            const auto now = std::chrono::steady_clock::now();
            if (renewed.Error().retryable && now < lease_deadline) {
                delay = std::min(std::chrono::milliseconds(2000), std::chrono::duration_cast<std::chrono::milliseconds>(lease_deadline - now));
                continue;
            }
            owner->TerminateCurrent(identity, control, renewed.Error().StableCode());
            co_return;
        }
        const auto grant = renewed.TakeValue();
        if (!HasSameConsoleFrontendIdentity(identity.expected_grant, grant)) {
            owner->TerminateCurrent(identity, control, "FRONTEND_LEASE_IDENTITY_CHANGED");
            co_return;
        }
        auto renewed_logical_grant = identity.logical_grant;
        const auto now_ms = CurrentSystemMilliseconds();
        renewed_logical_grant.expires_at_ms = now_ms + static_cast<std::int64_t>(grant.valid_for_ms);
        if (!transport->RenewLogicalSessionLease(renewed_logical_grant, now_ms)) {
            owner->TerminateCurrent(identity, control, "LOGICAL_LEASE_RENEWAL_REJECTED");
            co_return;
        }
        lease_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(grant.valid_for_ms);
        delay = RenewalDelay(grant.valid_for_ms);
    }
}

void WebSocketFrontendLeaseRenewalCoordinator::TerminateCurrent(const WebSocketFrontendLeaseIdentity& identity,
                                                                const std::shared_ptr<RenewalControl>& control, const std::string& reason) {
    if (!control->current.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (const auto transport = transport_.lock()) {
        if (!identity.allocation_id.empty()) {
            static_cast<void>(transport->RevokeLocalRtcInstance(identity.device_id, identity.stream_id, identity.allocation_id));
        }
        const auto close = std::make_shared<CloseLogicalSessionBindingEvent>();
        close->logical_session_id_ = identity.logical_grant.logical_session_id;
        close->binding_id_ = identity.binding_id;
        close->preserve_reconnect_grace_ = false;
        transport->EmitEvent(close);
    }
    if (identity.terminate_transport) {
        identity.terminate_transport();
    }
    LOGW(
        "event=session.lease component=net_ws operation=renew outcome=revoked code=LOGICAL_LEASE_REVOKED recoverable=false "
        "session={} reason={}",
        PrivacyLogId(identity.logical_grant.logical_session_id), reason);
    RemoveCurrent(control);
}

void WebSocketFrontendLeaseRenewalCoordinator::RemoveCurrent(const std::shared_ptr<RenewalControl>& expected_control) {
    std::scoped_lock lock(controls_mutex_);
    for (auto iterator = controls_.begin(); iterator != controls_.end();) {
        if (iterator->second == expected_control) {
            iterator = controls_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

}  // namespace px
