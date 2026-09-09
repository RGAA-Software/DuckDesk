#include "rdp_tcp_bridge.h"

#include <utility>

namespace px::rdp {
namespace {

void CloseSocket(const std::shared_ptr<asio::ip::tcp::socket>& socket) {
    if (socket) {
        asio::error_code error{};
        socket->cancel(error);
        socket->close(error);
    }
}

} // namespace

std::shared_ptr<RdpTcpBridge> RdpTcpBridge::Create(asio::any_io_executor executor, StreamBinding binding, Send send, Closed closed,
                                                   BridgeOptions options) {
    if (!binding.IsValid() || !send || !closed || options.max_pending_bytes < kMaxWireBytes || options.max_pending_bytes > 16 * 1024 * 1024 ||
        options.connect_timeout <= std::chrono::milliseconds::zero() || options.send_timeout <= std::chrono::milliseconds::zero()) {
        return {};
    }
    return std::make_shared<RdpTcpBridge>(ConstructionKey{}, std::move(executor), std::move(binding), std::move(send), std::move(closed), options);
}

RdpTcpBridge::RdpTcpBridge(ConstructionKey, asio::any_io_executor executor, StreamBinding binding, Send send, Closed closed, BridgeOptions options)
    : strand_(asio::make_strand(std::move(executor))), binding_(std::move(binding)), send_(std::move(send)), closed_(std::move(closed)),
      options_(options), deadline_(strand_) {}

RdpTcpBridge::~RdpTcpBridge() {
    // Keep the socket alive through serialized close, never capture this in deferred destruction.
    if (socket_) {
        asio::post(strand_, [socket = std::move(socket_)] { CloseSocket(socket); });
    }
}

void RdpTcpBridge::ConnectLoopback(const std::uint16_t port) {
    const auto weak = weak_from_this();
    asio::post(strand_, [weak, port] {
        const auto self = weak.lock();
        if (!self || self->finished_ || self->started_) {
            return;
        }
        self->started_ = true;
        if (port == 0 || self->stopping_.load()) {
            self->Finish(BridgeCloseReason::kConnectFailed, true);
            return;
        }
        self->socket_ = std::make_shared<asio::ip::tcp::socket>(self->strand_);
        self->deadline_.expires_after(self->options_.connect_timeout);
        self->deadline_.async_wait([weak](const asio::error_code& error) {
            if (const auto current = weak.lock(); current && !error) {
                current->Finish(BridgeCloseReason::kTimedOut, true);
            }
        });
        self->socket_->async_connect({asio::ip::address_v4::loopback(), port}, [weak](const asio::error_code& error) {
            const auto current = weak.lock();
            if (!current || current->finished_) {
                return;
            }
            current->deadline_.cancel();
            if (error) {
                current->Finish(BridgeCloseReason::kConnectFailed, true);
                return;
            }
            current->connected_ = true;
            current->ReadNext();
            current->WriteNext();
        });
    });
}

void RdpTcpBridge::Attach(std::shared_ptr<asio::ip::tcp::socket> socket) {
    asio::post(strand_, [weak = weak_from_this(), socket = std::move(socket)] {
        const auto self = weak.lock();
        if (!self || self->started_ || self->finished_ || self->stopping_.load()) {
            CloseSocket(socket);
            return;
        }
        self->started_ = true;
        if (!socket || !socket->is_open()) {
            self->Finish(BridgeCloseReason::kConnectFailed, true);
            return;
        }
        self->socket_ = socket;
        self->connected_ = true;
        self->ReadNext();
        self->WriteNext();
    });
}

bool RdpTcpBridge::Receive(std::shared_ptr<const Data> wire) {
    if (stopping_.load() || !wire) {
        return false;
    }
    const auto size = wire->Size();
    if (size == 0 || size > kMaxWireBytes) {
        stopping_.store(true);
        asio::post(strand_, [weak = weak_from_this()] {
            if (const auto self = weak.lock()) {
                self->Finish(BridgeCloseReason::kInvalidPacket, true);
            }
        });
        return false;
    }
    auto pending = pending_bytes_.load();
    do {
        if (size > options_.max_pending_bytes - pending) {
            stopping_.store(true);
            asio::post(strand_, [weak = weak_from_this()] {
                if (const auto self = weak.lock()) {
                    self->Finish(BridgeCloseReason::kQueueFull, true);
                }
            });
            return false;
        }
    } while (!pending_bytes_.compare_exchange_weak(pending, pending + size));
    asio::post(strand_, [weak = weak_from_this(), wire = std::move(wire)] {
        if (const auto self = weak.lock()) {
            if (!self->OnPacket(wire)) {
                self->pending_bytes_.fetch_sub(wire->Size());
            }
        }
    });
    return true;
}

bool RdpTcpBridge::OnPacket(const std::shared_ptr<const Data>& wire) {
    if (finished_ || stopping_.load()) {
        return false;
    }
    const auto packet = DecodePacket(binding_, wire->Bytes());
    if (packet.status == PacketStatus::kStaleBinding || packet.status == PacketStatus::kOtherMessage) {
        return false;
    }
    if (packet.status == PacketStatus::kClose) {
        Finish(BridgeCloseReason::kPeerClosed, false);
        return false;
    }
    if (packet.status != PacketStatus::kData || !packet.payload) {
        Finish(BridgeCloseReason::kInvalidPacket, true);
        return false;
    }
    // Retain the original wire reservation until the TCP write finishes.
    // Tasks awaiting the strand and writes awaiting TCP share the same budget.
    incoming_.push_back({packet.payload, wire->Size()});
    WriteNext();
    return true;
}

void RdpTcpBridge::ReadNext() {
    if (!connected_ || finished_ || stopping_.load()) {
        return;
    }
    const auto buffer = Data::Allocate(kMaxPayloadBytes);
    const auto weak = weak_from_this();
    socket_->async_read_some(asio::buffer(buffer->MutableBytes().data(), buffer->Size()),
                             asio::bind_executor(strand_, [weak, buffer](const asio::error_code& error, const std::size_t count) {
                                 const auto self = weak.lock();
                                 if (!self || self->finished_) {
                                     return;
                                 }
                                 if (error || count == 0) {
                                     self->Finish(BridgeCloseReason::kTcpClosed, true);
                                     return;
                                 }
                                 const auto wire = EncodeData(self->binding_, buffer->Bytes().first(count));
                                 const auto send_id = ++self->send_id_;
                                 self->sending_ = true;
                                 self->deadline_.expires_after(self->options_.send_timeout);
                                 self->deadline_.async_wait([weak, send_id](const asio::error_code& timeout_error) {
                                     if (const auto current = weak.lock();
                                         current && !timeout_error && current->sending_ && current->send_id_ == send_id) {
                                         current->Finish(BridgeCloseReason::kTimedOut, true);
                                     }
                                 });
                                 try {
                                     self->send_(wire, [weak, send_id](const bool success) {
                                         if (const auto current = weak.lock()) {
                                             asio::post(current->strand_, [weak, send_id, success] {
                                                 if (const auto owner = weak.lock()) {
                                                     owner->OnSent(send_id, success);
                                                 }
                                             });
                                         }
                                     });
                                 } catch (...) {
                                     self->Finish(BridgeCloseReason::kSendFailed, false);
                                 }
                             }));
}

void RdpTcpBridge::OnSent(const std::uint64_t send_id, const bool success) {
    if (finished_ || !sending_ || send_id != send_id_) {
        return;
    }
    sending_ = false;
    deadline_.cancel();
    if (!success) {
        Finish(BridgeCloseReason::kSendFailed, false);
        return;
    }
    ReadNext();
}

void RdpTcpBridge::WriteNext() {
    if (finished_ || !connected_ || writing_ || incoming_.empty() || stopping_.load()) {
        return;
    }
    writing_ = true;
    const auto payload = incoming_.front().payload;
    asio::async_write(*socket_, asio::buffer(payload->Bytes().data(), payload->Size()),
                      asio::bind_executor(strand_, [weak = weak_from_this(), payload](const asio::error_code& error, const std::size_t count) {
                          const auto self = weak.lock();
                          if (!self || self->finished_) {
                              return;
                          }
                          self->writing_ = false;
                          if (error || count != payload->Size()) {
                              self->Finish(BridgeCloseReason::kTcpClosed, true);
                              return;
                          }
                          self->pending_bytes_.fetch_sub(self->incoming_.front().reservation);
                          self->incoming_.pop_front();
                          self->WriteNext();
                      }));
}

void RdpTcpBridge::Stop() {
    stopping_.store(true);
    asio::post(strand_, [weak = weak_from_this()] {
        if (const auto self = weak.lock()) {
            self->Finish(BridgeCloseReason::kStopped, true);
        }
    });
}

void RdpTcpBridge::Finish(const BridgeCloseReason reason, const bool notify_peer) {
    if (finished_) {
        return;
    }
    finished_ = true;
    stopping_.store(true);
    deadline_.cancel();
    CloseSocket(socket_);
    for (const auto& pending : incoming_) {
        pending_bytes_.fetch_sub(pending.reservation);
    }
    incoming_.clear();
    if (notify_peer) {
        try {
            send_(EncodeClose(binding_), [](bool) {});
        } catch (...) {
            // Termination is best effort; a dead WS cannot acknowledge a close.
        }
    }
    const auto closed = std::move(closed_);
    send_ = {};
    if (closed) {
        try {
            closed(reason);
        } catch (...) {
            // Application notification must not unwind the shared network executor.
        }
    }
}

std::size_t RdpTcpBridge::PendingBytes() const noexcept {
    return pending_bytes_.load();
}

} // namespace px::rdp
