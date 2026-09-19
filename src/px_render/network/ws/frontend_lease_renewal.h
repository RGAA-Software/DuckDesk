#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "px_common/async_runtime.h"
#include "px_render/network/transport_types.h"
#include "px_render/session/logical_session_registry.h"

namespace px {

class WsTransport;

class WebSocketFrontendToken final {
public:
    explicit WebSocketFrontendToken(std::string value);
    ~WebSocketFrontendToken();

    WebSocketFrontendToken(const WebSocketFrontendToken&) = delete;
    WebSocketFrontendToken& operator=(const WebSocketFrontendToken&) = delete;

    [[nodiscard]] std::string Copy() const;

private:
    std::string value_{};
};

struct WebSocketFrontendDescriptor final {
    std::string session_id{};
    std::int64_t revision{};
    std::string stream_id{};
    std::shared_ptr<WebSocketFrontendToken> token{};
};

[[nodiscard]] std::optional<WebSocketFrontendDescriptor> ConsumeWebSocketFrontendDescriptor(
    std::unordered_map<std::string, std::string>& query_parameters);
[[nodiscard]] bool IsAcceptedWebSocketFrontendGrant(const WebSocketFrontendDescriptor& descriptor, const std::string& render_instance_id,
                                                    const ConsoleFrontendGrant& grant);
[[nodiscard]] bool HasSameConsoleFrontendIdentity(const ConsoleFrontendGrant& expected, const ConsoleFrontendGrant& renewed);

struct WebSocketFrontendLeaseIdentity final {
    ConsoleFrontendGrant expected_grant{};
    LogicalSessionGrant logical_grant{};
    std::string descriptor_session_id{};
    std::int64_t descriptor_revision{};
    std::string device_id{};
    std::string stream_id{};
    std::string allocation_id{};
    std::string binding_id{};
    std::function<void()> terminate_transport{};
};

class WebSocketFrontendLeaseRenewalCoordinator final : public std::enable_shared_from_this<WebSocketFrontendLeaseRenewalCoordinator> {
public:
    WebSocketFrontendLeaseRenewalCoordinator(std::weak_ptr<WsTransport> transport, std::shared_ptr<PxAsyncScope> async_scope);

    void Start(WebSocketFrontendLeaseIdentity identity, std::shared_ptr<WebSocketFrontendToken> token, std::uint32_t initial_valid_for_ms);
    void Cancel(const std::string& binding_id);

private:
    struct RenewalControl final {
        std::atomic_bool current{true};
    };

    static PxAwaitable<void> Run(std::weak_ptr<WebSocketFrontendLeaseRenewalCoordinator> owner, std::shared_ptr<RenewalControl> control,
                                 WebSocketFrontendLeaseIdentity identity, std::shared_ptr<WebSocketFrontendToken> token, std::uint32_t valid_for_ms);
    void TerminateCurrent(const WebSocketFrontendLeaseIdentity& identity, const std::shared_ptr<RenewalControl>& control, const std::string& reason);
    void RemoveCurrent(const std::shared_ptr<RenewalControl>& expected_control);

    std::weak_ptr<WsTransport> transport_{};
    std::shared_ptr<PxAsyncScope> async_scope_{};
    std::mutex controls_mutex_{};
    std::unordered_map<std::string, std::shared_ptr<RenewalControl>> controls_{};
};

}  // namespace px
