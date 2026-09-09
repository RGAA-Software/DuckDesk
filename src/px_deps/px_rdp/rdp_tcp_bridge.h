#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>

#include "px_common/async_runtime.h"
#include "rdp_stream_packet.h"

namespace px::rdp {

enum class BridgeCloseReason { kStopped, kPeerClosed, kTcpClosed, kConnectFailed, kTimedOut, kInvalidPacket, kQueueFull, kSendFailed };

struct BridgeOptions final {
    std::size_t max_pending_bytes{2 * 1024 * 1024};
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds send_timeout{5000};
};

// A single RDP byte stream, not a WebSocket connection or an authorization service.
// The caller supplies an already-authorized binding and routes ONLY kRdpStream here.
// Send completion means the WS write completed, not merely that it was queued.
// Only one read/send is in flight, so outbound traffic cannot grow an unbounded WS queue.
class RdpTcpBridge final : public std::enable_shared_from_this<RdpTcpBridge> {
    struct ConstructionKey final {};

  public:
    using SendCompletion = std::function<void(bool)>;
    using Send = std::function<void(std::shared_ptr<Data>, SendCompletion)>;
    using Closed = std::function<void(BridgeCloseReason)>;

    [[nodiscard]] static std::shared_ptr<RdpTcpBridge> Create(asio::any_io_executor executor, StreamBinding binding, Send send, Closed closed,
                                                              BridgeOptions options = {});
    RdpTcpBridge(ConstructionKey, asio::any_io_executor executor, StreamBinding binding, Send send, Closed closed, BridgeOptions options);
    ~RdpTcpBridge();

    // Only a composition-root selected loopback port may be used; no client-supplied remote target.
    void ConnectLoopback(std::uint16_t port);
    // For a protected local FreeRDP adapter socket. Ownership transfers to the bridge.
    void Attach(std::shared_ptr<asio::ip::tcp::socket> socket);
    [[nodiscard]] bool Receive(std::shared_ptr<const Data> wire);
    void Stop();
    [[nodiscard]] std::size_t PendingBytes() const noexcept;

  private:
    void ReadNext();
    void WriteNext();
    void Finish(BridgeCloseReason reason, bool notify_peer);
    [[nodiscard]] bool OnPacket(const std::shared_ptr<const Data>& wire);
    void OnSent(std::uint64_t send_id, bool success);

    asio::strand<asio::any_io_executor> strand_;
    StreamBinding binding_{};
    Send send_{};
    Closed closed_{};
    BridgeOptions options_{};
    std::shared_ptr<asio::ip::tcp::socket> socket_{};
    asio::steady_timer deadline_;
    struct PendingWrite final {
        std::shared_ptr<const Data> payload{};
        std::size_t reservation{0};
    };
    std::deque<PendingWrite> incoming_{};
    std::atomic_size_t pending_bytes_{0};
    std::atomic_bool stopping_{false};
    std::uint64_t send_id_{0};
    bool started_{false};
    bool connected_{false};
    bool finished_{false};
    bool writing_{false};
    bool sending_{false};
};

} // namespace px::rdp
