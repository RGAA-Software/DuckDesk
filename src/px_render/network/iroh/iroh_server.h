#pragma once

#include "iroh_frontend.h"

namespace px {
class Thread;

struct AcceptedIrohFrontend final {
    std::shared_ptr<IrohFrontend> frontend{};
    std::shared_ptr<transport::Connection> connection{};
    std::shared_ptr<transport::Channel> control{};
    transport::SessionChannels channels{};
};

// Listening and frontend admission only. Message/media/RDP routing belongs to the accepted-session owner.
class IrohServer final : public std::enable_shared_from_this<IrohServer> {
public:
    using AcceptedCallback = std::function<bool(AcceptedIrohFrontend)>;
    IrohServer(std::weak_ptr<WsTransport> services, std::shared_ptr<PxAsyncRuntime> runtime, bool rdp, AcceptedCallback accepted,
               RenderEventCallback events);
    ~IrohServer();
    [[nodiscard]] bool Start(const std::string& endpoint_configuration);
    void Stop();
    [[nodiscard]] std::expected<std::string, transport::Error> Address() const;
    [[nodiscard]] bool UpdateRelays(const std::string& relays_json);

private:
    struct PendingPeer final {
        std::shared_ptr<transport::Connection> connection{};
        std::shared_ptr<Thread> worker{};
        std::atomic_bool done{};
    };
    static void AcceptConnections(std::weak_ptr<IrohServer> owner, std::shared_ptr<transport::Endpoint> endpoint);
    static PxAwaitable<void> MonitorConnections(std::weak_ptr<IrohServer> owner);
    static void OpenPeer(std::weak_ptr<IrohServer> owner, std::shared_ptr<PendingPeer> peer);
    void PruneConnections();
    std::weak_ptr<WsTransport> services_{};
    std::shared_ptr<PxAsyncScope> scope_{};
    std::shared_ptr<WebSocketFrontendLeaseRenewalCoordinator> leases_{};
    bool rdp_{};
    AcceptedCallback accepted_{};
    RenderEventCallback events_{};
    mutable std::mutex mutex_{};
    std::shared_ptr<transport::Endpoint> endpoint_{};
    std::shared_ptr<Thread> accept_worker_{};
    std::vector<std::shared_ptr<PendingPeer>> pending_{};
    std::vector<std::shared_ptr<IrohFrontend>> active_{};
    std::weak_ptr<IrohFrontend> rdp_frontend_{};
    std::mutex rdp_admission_mutex_{};
    std::atomic_bool stopped_{true};
    bool started_once_{};
};
}  // namespace px
