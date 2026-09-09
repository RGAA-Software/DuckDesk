#include "rdp_client_endpoint.h"

#include <Windows.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <cstring>
#include <vector>

namespace px::rdp {
namespace {

bool SameProcessPeer(const asio::ip::tcp::socket& socket) {
    asio::error_code error{};
    const auto peer = socket.remote_endpoint(error);
    if (error || peer.address() != asio::ip::address_v4::loopback()) {
        return false;
    }
    const auto local = socket.local_endpoint(error);
    if (error || local.address() != asio::ip::address_v4::loopback()) {
        return false;
    }
    DWORD size{};
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != ERROR_INSUFFICIENT_BUFFER ||
        size < sizeof(DWORD) || size > 16 * 1024 * 1024) {
        return false;
    }
    auto bytes = std::vector<unsigned char>(size);
    if (GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != NO_ERROR) {
        return false;
    }
    DWORD count{};
    std::memcpy(&count, bytes.data(), sizeof(count));
    constexpr auto offset = offsetof(MIB_TCPTABLE_OWNER_PID, table);
    if (bytes.size() < offset || count > (bytes.size() - offset) / sizeof(MIB_TCPROW_OWNER_PID)) {
        return false;
    }
    for (DWORD index{}; index < count; ++index) {
        MIB_TCPROW_OWNER_PID row{};
        std::memcpy(&row, bytes.data() + offset + index * sizeof(row), sizeof(row));
        if (row.dwOwningPid == GetCurrentProcessId() && row.dwLocalAddr == 0x0100007f && row.dwRemoteAddr == 0x0100007f &&
            ntohs(static_cast<u_short>(row.dwLocalPort)) == peer.port() && ntohs(static_cast<u_short>(row.dwRemotePort)) == local.port() &&
            row.dwState == MIB_TCP_STATE_ESTAB) {
            return true;
        }
    }
    return false;
}

void CloseListener(const std::shared_ptr<asio::ip::tcp::acceptor>& acceptor, const std::shared_ptr<asio::steady_timer>& deadline) {
    asio::error_code error{};
    acceptor->cancel(error);
    acceptor->close(error);
    deadline->cancel();
}

} // namespace

std::shared_ptr<RdpClientEndpoint> RdpClientEndpoint::Create(asio::any_io_executor executor, StreamBinding binding, RdpTcpBridge::Send send,
                                                             Ready ready, RdpTcpBridge::Closed closed, BridgeOptions options) {
    if (!ready || !closed) {
        return {};
    }
    auto self = std::make_shared<RdpClientEndpoint>(ConstructionKey{}, executor, std::move(ready), std::move(closed), options);
    const auto weak = std::weak_ptr<RdpClientEndpoint>{self};
    self->bridge_ = RdpTcpBridge::Create(
        executor, std::move(binding), std::move(send),
        [weak](BridgeCloseReason reason) {
            if (const auto owner = weak.lock()) {
                asio::post(owner->strand_, [weak, reason] {
                    if (const auto current = weak.lock()) {
                        current->Finish(reason);
                    }
                });
            }
        },
        options);
    if (!self->bridge_) {
        return {};
    }
    asio::post(self->strand_, [weak] {
        if (const auto current = weak.lock()) {
            current->Start();
        }
    });
    return self;
}

RdpClientEndpoint::RdpClientEndpoint(ConstructionKey, asio::any_io_executor executor, Ready ready, RdpTcpBridge::Closed closed, BridgeOptions options)
    : strand_(asio::make_strand(std::move(executor))), acceptor_(std::make_shared<asio::ip::tcp::acceptor>(strand_)),
      deadline_(std::make_shared<asio::steady_timer>(strand_)), ready_(std::move(ready)), closed_(std::move(closed)), options_(options) {}

RdpClientEndpoint::~RdpClientEndpoint() {
    // No asynchronous access to a destroyed endpoint, including during partial factory failure.
    asio::post(strand_, [acceptor = std::move(acceptor_), deadline = std::move(deadline_), bridge = std::move(bridge_)] {
        CloseListener(acceptor, deadline);
        if (bridge) {
            bridge->Stop();
        }
    });
}

void RdpClientEndpoint::Start() {
    if (stopping_.load()) {
        Finish(BridgeCloseReason::kStopped);
        return;
    }
    asio::error_code error{};
    acceptor_->open(asio::ip::tcp::v4(), error);
    if (!error) {
        acceptor_->bind({asio::ip::address_v4::loopback(), 0}, error);
    }
    if (!error) {
        acceptor_->listen(1, error);
    }
    if (error) {
        Finish(BridgeCloseReason::kConnectFailed);
        return;
    }
    const auto endpoint = acceptor_->local_endpoint(error);
    if (error) {
        Finish(BridgeCloseReason::kConnectFailed);
        return;
    }
    deadline_->expires_after(options_.connect_timeout);
    deadline_->async_wait([weak = weak_from_this()](const asio::error_code& timed_out) {
        if (const auto self = weak.lock(); self && !timed_out) {
            self->Finish(BridgeCloseReason::kTimedOut);
        }
    });
    AcceptNext();
    const auto ready = std::move(ready_);
    try {
        ready(endpoint.port());
    } catch (...) {
        Finish(BridgeCloseReason::kConnectFailed);
    }
}

void RdpClientEndpoint::AcceptNext() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(strand_);
    acceptor_->async_accept(*socket, [weak = weak_from_this(), socket](const asio::error_code& error) {
        const auto self = weak.lock();
        if (!self || self->finished_ || self->stopping_.load()) {
            return;
        }
        if (error) {
            self->Finish(BridgeCloseReason::kConnectFailed);
            return;
        }
        if (!SameProcessPeer(*socket)) {
            asio::error_code ignored{};
            socket->close(ignored);
            if (++self->rejected_peers_ >= 8) {
                self->Finish(BridgeCloseReason::kConnectFailed);
            } else {
                self->AcceptNext();
            }
            return;
        }
        CloseListener(self->acceptor_, self->deadline_);
        self->bridge_->Attach(socket);
    });
}

bool RdpClientEndpoint::Receive(std::shared_ptr<const Data> wire) {
    return !stopping_.load() && bridge_->Receive(std::move(wire));
}

void RdpClientEndpoint::Stop() {
    stopping_.store(true);
    asio::post(strand_, [weak = weak_from_this()] {
        if (const auto self = weak.lock()) {
            self->Finish(BridgeCloseReason::kStopped);
        }
    });
}

void RdpClientEndpoint::Finish(BridgeCloseReason reason) {
    if (finished_) {
        return;
    }
    finished_ = true;
    stopping_.store(true);
    CloseListener(acceptor_, deadline_);
    bridge_->Stop();
    ready_ = {};
    const auto closed = std::move(closed_);
    try {
        if (closed) {
            closed(reason);
        }
    } catch (...) {
        // Application callbacks must not unwind the shared executor.
    }
}

} // namespace px::rdp
