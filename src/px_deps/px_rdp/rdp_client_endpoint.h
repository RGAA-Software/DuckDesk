#pragma once

#include "rdp_tcp_bridge.h"

namespace px::rdp {

// Per-connection bridge for the in-process Windows FreeRDP adapter. The listener
// is loopback-only and admits only the reverse TCP tuple owned by this process.
// A fresh object is required after every authorized WebSocket OPEN; neither NLA
// state nor queued bytes are carried across reconnections.
class RdpClientEndpoint final : public std::enable_shared_from_this<RdpClientEndpoint> {
    struct ConstructionKey final {};

  public:
    using Ready = std::function<void(std::uint16_t)>;
    [[nodiscard]] static std::shared_ptr<RdpClientEndpoint> Create(asio::any_io_executor executor, StreamBinding binding, RdpTcpBridge::Send send,
                                                                   Ready ready, RdpTcpBridge::Closed closed, BridgeOptions options = {});
    RdpClientEndpoint(ConstructionKey, asio::any_io_executor executor, Ready ready, RdpTcpBridge::Closed closed, BridgeOptions options);
    ~RdpClientEndpoint();
    [[nodiscard]] bool Receive(std::shared_ptr<const Data> wire);
    void Stop();

  private:
    void Start();
    void AcceptNext();
    void Finish(BridgeCloseReason reason);

    asio::strand<asio::any_io_executor> strand_;
    std::shared_ptr<asio::ip::tcp::acceptor> acceptor_{};
    std::shared_ptr<asio::steady_timer> deadline_{};
    std::shared_ptr<RdpTcpBridge> bridge_{};
    Ready ready_{};
    RdpTcpBridge::Closed closed_{};
    BridgeOptions options_{};
    std::atomic_bool stopping_{false};
    unsigned int rejected_peers_{0};
    bool finished_{false};
};

} // namespace px::rdp
