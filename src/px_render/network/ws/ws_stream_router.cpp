//
// Created by RGAA on 2024/3/5.
//

#include "ws_stream_router.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "px_common/privacy_log.h"
#include "px_common/thread_util.h"
#include "px_common/ws_control_signal.h"
#include "px_common/reliable_websocket_send.h"
#include "ws_transport.h"
#include "px_message.pb.h"

namespace px {

WsStreamRouter::~WsStreamRouter() {
    if (rdp_bridge_) {
        rdp_bridge_->Stop();
    }
    if (const auto release = std::exchange(rdp_release_, {})) {
        release();
    }
}

void WsStreamRouter::OnOpen(std::shared_ptr<asio2::http_session>& sess_ptr) {
    {
        std::lock_guard lock(reliable_session_mutex_);
        reliable_session_ = sess_ptr;
    }
    WsRouter::OnOpen(sess_ptr);
}

void WsStreamRouter::OnClose(std::shared_ptr<asio2::http_session>& sess_ptr) {
    {
        std::lock_guard lock(reliable_session_mutex_);
        reliable_session_.reset();
    }
    if (rdp_bridge_) {
        rdp_bridge_->Stop();
        rdp_bridge_.reset();
    }
    if (const auto release = std::exchange(rdp_release_, {})) {
        release();
    }
    NotifyClosed();
    WsRouter::OnClose(sess_ptr);
}

void WsStreamRouter::OnMessage(std::shared_ptr<asio2::http_session>& sess_ptr, int64_t socket_fd, std::string_view data) {
    WsRouter::OnMessage(sess_ptr, socket_fd, data);
    if (rdp_mode_.load()) {
        Message envelope{};
        if (data.size() > rdp::kMaxWireBytes || !envelope.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
            sess_ptr->stop();
            return;
        }
        if (envelope.type() == kRdpStream && rdp_bridge_) {
            if (!rdp_bridge_->Receive(Data::From(data))) {
                sess_ptr->stop();
            }
        } else if (envelope.type() == kHeartBeat && envelope.has_heartbeat()) {
            // Never expose the host desktop, monitors, clipboard, files or input handlers through this workspace.
            Message reply{};
            reply.set_type(kOnHeartBeat);
            auto& heartbeat = *reply.mutable_on_heartbeat();
            heartbeat.set_timestamp(envelope.heartbeat().timestamp());
            PostReliableBinaryMessage(Data::From(reply.SerializeAsString()), [](bool) {});
        } else {
            sess_ptr->stop();
        }
        return;
    }
    if (IsWsUseWebSocketMediaSignal(data)) {
        if (udp_media_.exchange(false)) {
            LOGI("event=transport.route component=net_ws operation=udp_fallback "
                 "outcome=websocket stream={}",
                 PrivacyLogId(stream_id_));
            if (udp_media_fallback_callback_) {
                udp_media_fallback_callback_();
            }
        }
        return;
    }
    px::Message parsed;
    if (parsed.ParseFromArray(data.data(), static_cast<int>(data.size())) &&
        (parsed.type() == kApplicationTextCapabilities || parsed.type() == kApplicationTextSubmit || parsed.type() == kApplicationTextBarrier) &&
        !input_allowed_.load()) {
        return;
    }
    if (parsed.ParsePartialFromArray(data.data(), static_cast<int>(data.size())) &&
        (parsed.type() == MessageType::kFileAction || parsed.type() == MessageType::kFileResponse) && !file_allowed_.load()) {
        const auto decision = permission_log_gate_.Evaluate("file_transfer", std::chrono::steady_clock::now());
        if (decision.emit) {
            LOGW("event=transport.receive component=net_ws "
                 "code=SESSION_CAPABILITY_DENIED operation=file_transfer "
                 "outcome=dropped recoverable=true stream={} suppressed={}",
                 PrivacyLogId(stream_id_), decision.suppressed_since_last_emit);
        }
        return;
    }
    const auto transport = ws_data_ ? ws_data_->transport_.lock() : nullptr;
    if (!transport) {
        return;
    }
    auto msg = Data::From(data);
    transport->ReceiveClientEvent(true, socket_fd, TransportKind::kWebSocket, channel_type_, msg, binding_id_);
}

void WsStreamRouter::OnPing(std::shared_ptr<asio2::http_session>& sess_ptr) {
    WsRouter::OnPing(sess_ptr);
}

void WsStreamRouter::OnPong(std::shared_ptr<asio2::http_session>& sess_ptr) {
    WsRouter::OnPong(sess_ptr);
}

void WsStreamRouter::PostReliableBinaryMessage(std::shared_ptr<Data> data, std::function<void(bool)> completion) {
    auto session = std::shared_ptr<asio2::http_session>{};
    {
        std::lock_guard lock(reliable_session_mutex_);
        session = reliable_session_.lock();
    }
    PostReliableWebSocketWrite(session, std::move(data), std::move(completion),
                              [weak = weak_from_this(), weak_session = std::weak_ptr<asio2::http_session>(session)] {
        const auto self = weak.lock();
        if (!self) {
            return false;
        }
        std::lock_guard lock(self->reliable_session_mutex_);
        return self->reliable_session_.lock() == weak_session.lock();
    });
}

void WsStreamRouter::PostBinaryMessage(std::shared_ptr<Data> data) {
    if (rdp_mode_.load()) {
        return; // RDP never participates in native broadcast or host feature routing.
    }
    if (!session_ || !session_->is_started()) {
        return;
    }

    auto tid = px::GetCurrentThreadID();
    if (post_thread_id_ == 0) {
        post_thread_id_ = tid;
    }
    if (tid != post_thread_id_) {
        // LOGI("OH NO! Post binary message in thread: {}, but the last thread is: {}", tid, post_thread_id_);
    }

    session_->ws_stream().binary(true);
    queuing_message_count_++;
    auto weak_self = weak_from_this();
    // asio2 consumes this buffer asynchronously. Retain the owned payload
    // until completion so short-lived control and voice messages cannot
    // leave the socket with a dangling byte view.
    session_->async_send(data->Bytes().data(), data->Size(), [weak_self, data](size_t byte_sent) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        const auto remaining = --self->queuing_message_count_;
        if (remaining <= kFileTransferQueueLowWatermark) {
            self->NotifyWritable();
        }

        // report data size
        const auto transport = self->ws_data_ ? self->ws_data_->transport_.lock() : nullptr;
        if (transport) {
            transport->ReportDataSent(byte_sent);
        }
    });
}

bool WsStreamRouter::StartRdp(asio::any_io_executor executor, const std::uint16_t proxy_port, std::function<void()> release,
                              std::function<void()> closed) {
    if (proxy_port == 0 || rdp_mode_.exchange(true) || !session_) {
        if (release) {
            release();
        }
        return false;
    }
    rdp_release_ = std::move(release);
    const rdp::StreamBinding binding{.connection_id = GetUUID(), .generation = 1};
    const auto weak = weak_from_this();
    const auto weak_session = std::weak_ptr<asio2::http_session>(session_);
    rdp_bridge_ = rdp::RdpTcpBridge::Create(
        std::move(executor), binding,
        [weak](std::shared_ptr<Data> wire, rdp::RdpTcpBridge::SendCompletion completion) {
            if (const auto self = weak.lock()) {
                self->PostReliableBinaryMessage(std::move(wire), std::move(completion));
            } else {
                completion(false);
            }
        },
        [weak_session, closed = std::move(closed)](rdp::BridgeCloseReason reason) {
            if (const auto session = weak_session.lock()) {
                session->post([weak_session, closed, reason] {
                    if (const auto active = weak_session.lock()) {
                        // The RDP TCP endpoint is already closed. Asio2 may still
                        // be draining a WebSocket close handshake; do not retain
                        // the application seat until that transport drain ends.
                        LOGI("event=rdp.route.closed reason={}", static_cast<int>(reason));
                        if (closed) {
                            closed();
                        }
                        active->stop();
                    }
                });
            }
        });
    if (!rdp_bridge_) {
        return false;
    }
    const auto weak_bridge = std::weak_ptr<rdp::RdpTcpBridge>(rdp_bridge_);
    PostReliableBinaryMessage(rdp::EncodeOpen(binding), [weak_bridge, proxy_port](bool sent) {
        if (const auto bridge = weak_bridge.lock()) {
            if (sent) {
                bridge->ConnectLoopback(proxy_port);
            } else {
                bridge->Stop();
            }
        }
    });
    return true;
}

void WsStreamRouter::PostBinaryMessage(const std::string& data) {
    this->PostBinaryMessage(Data::From(data));
}

void WsStreamRouter::PostTextMessage(const std::string& data) {
    if (rdp_mode_.load() || !session_ || !session_->is_started()) {
        return;
    }

    auto tid = px::GetCurrentThreadID();
    if (post_thread_id_ == 0) {
        post_thread_id_ = tid;
    }
    if (tid != post_thread_id_) {
        LOGI("OH NO! Post text message in thread: {}, but the last thread is: {}", tid, post_thread_id_);
    }

    session_->ws_stream().text(true);
    queuing_message_count_++;
    auto weak_self = weak_from_this();
    session_->async_send(data, [weak_self](size_t byte_sent) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        self->queuing_message_count_--;

        // report data size
        const auto transport = self->ws_data_ ? self->ws_data_->transport_.lock() : nullptr;
        if (transport) {
            transport->ReportDataSent(byte_sent);
        }
    });
}

FileTransferSendResult WsStreamRouter::TryPostFileTransferMessage(const std::shared_ptr<Data>& data) {
    if (rdp_mode_.load()) {
        return FileTransferSendResult::Disconnected("Host file transfer is unavailable in RDP mode");
    }
    if (!file_allowed_.load()) {
        return FileTransferSendResult::Disconnected("WebSocket control session has no file-transfer capability");
    }
    if (!data) {
        return FileTransferSendResult::TransportError("WebSocket file-transfer payload is empty");
    }
    if (!session_ || !session_->is_started()) {
        return FileTransferSendResult::Disconnected("WebSocket control session is not connected");
    }
    if (GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
        return FileTransferSendResult::Busy("WebSocket control queue is full", AcquireWritableSignal());
    }
    PostBinaryMessage(data);
    return FileTransferSendResult::Accepted();
}

void WsStreamRouter::SetUdpMediaFallbackCallback(std::function<void()> callback) {
    udp_media_fallback_callback_ = std::move(callback);
}

void WsStreamRouter::RevokeRdp() {
    std::weak_ptr<asio2::http_session> weak_session{};
    {
        std::lock_guard lock(reliable_session_mutex_);
        weak_session = reliable_session_;
    }
    if (const auto session = weak_session.lock()) {
        session->post([weak_session] {
            if (const auto current = weak_session.lock()) {
                current->stop();
            }
        });
    }
}

std::shared_ptr<FileTransferWritableSignal> WsStreamRouter::AcquireWritableSignal() {
    std::shared_ptr<FileTransferWritableSignal> signal;
    {
        std::lock_guard lock(writable_signal_mutex_);
        if (!writable_signal_ || writable_signal_->outcome() != FileTransferWritableOutcome::kPending) {
            writable_signal_ = FileTransferWritableSignal::Create();
        }
        signal = writable_signal_;
    }
    if (GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
        signal->NotifyWritable();
    }
    return signal;
}

void WsStreamRouter::NotifyWritable() {
    std::shared_ptr<FileTransferWritableSignal> signal;
    {
        std::lock_guard lock(writable_signal_mutex_);
        signal = std::move(writable_signal_);
    }
    if (signal) {
        signal->NotifyWritable();
    }
}

void WsStreamRouter::NotifyClosed() {
    std::shared_ptr<FileTransferWritableSignal> signal;
    {
        std::lock_guard lock(writable_signal_mutex_);
        signal = std::move(writable_signal_);
    }
    if (signal) {
        signal->Close();
    }
}
} // namespace px
