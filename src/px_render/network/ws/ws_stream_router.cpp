//
// Created by RGAA on 2024/3/5.
//

#include "ws_stream_router.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "px_common/privacy_log.h"
#include "px_common/thread_util.h"
#include "px_common/ws_control_signal.h"
#include "ws_transport.h"
#include "px_message.pb.h"

namespace px
{

    void WsStreamRouter::OnOpen(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnOpen(sess_ptr);
    }

    void WsStreamRouter::OnClose(std::shared_ptr<asio2::http_session> &sess_ptr) {
        NotifyClosed();
        WsRouter::OnClose(sess_ptr);
    }

    void WsStreamRouter::OnMessage(std::shared_ptr<asio2::http_session>& sess_ptr, int64_t socket_fd, std::string_view data) {
        WsRouter::OnMessage(sess_ptr, socket_fd, data);
        if (IsWsUseWebSocketMediaSignal(data)) {
            if (udp_media_.exchange(false)) {
                LOGI("event=transport.route component=net_ws operation=udp_fallback "
                     "outcome=websocket stream={}", PrivacyLogId(stream_id_));
                if (udp_media_fallback_callback_) {
                    udp_media_fallback_callback_();
                }
            }
            return;
        }
        px::Message parsed;
        if (parsed.ParsePartialFromArray(data.data(), static_cast<int>(data.size()))
            && (parsed.type() == MessageType::kFileAction
                || parsed.type() == MessageType::kFileResponse)
            && !file_allowed_.load()) {
            const auto decision = permission_log_gate_.Evaluate(
                "file_transfer", std::chrono::steady_clock::now());
            if (decision.emit) {
                LOGW("event=transport.receive component=net_ws "
                     "code=SESSION_CAPABILITY_DENIED operation=file_transfer "
                     "outcome=dropped recoverable=true stream={} suppressed={}",
                     PrivacyLogId(stream_id_),
                     decision.suppressed_since_last_emit);
            }
            return;
        }
        const auto transport = ws_data_ ? ws_data_->transport_.lock() : nullptr;
        if (!transport) {
            return;
        }
        auto msg = Data::From(data);
        transport->ReceiveClientEvent(
            true, socket_fd, TransportKind::kWebSocket, channel_type_, msg, binding_id_);
    }

    void WsStreamRouter::OnPing(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPing(sess_ptr);
    }

    void WsStreamRouter::OnPong(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPong(sess_ptr);
    }

    void WsStreamRouter::PostBinaryMessage(std::shared_ptr<Data> data) {
        if (!session_ || !session_->is_started()) {
            return;
        }

        auto tid = px::GetCurrentThreadID();
        if (post_thread_id_ == 0) {
            post_thread_id_ = tid;
        }
        if (tid != post_thread_id_) {
            //LOGI("OH NO! Post binary message in thread: {}, but the last thread is: {}", tid, post_thread_id_);
        }

        session_->ws_stream().binary(true);
        queuing_message_count_++;
        auto weak_self = weak_from_this();
        session_->async_send(data->Bytes().data(), data->Size(), [weak_self](size_t byte_sent) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            const auto remaining = --self->queuing_message_count_;
            if (remaining <= kFileTransferQueueLowWatermark) {
                self->NotifyWritable();
            }

            // report data size
            const auto transport = self->ws_data_
                ? self->ws_data_->transport_.lock() : nullptr;
            if (transport) {
                transport->ReportDataSent(byte_sent);
            }
        });
    }

    void WsStreamRouter::PostBinaryMessage(const std::string &data) {
        this->PostBinaryMessage(Data::From(data));
    }

    void WsStreamRouter::PostTextMessage(const std::string &data) {
        if (!session_ || !session_->is_started()) {
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
            const auto transport = self->ws_data_
                ? self->ws_data_->transport_.lock() : nullptr;
            if (transport) {
                transport->ReportDataSent(byte_sent);
            }
        });
    }

    FileTransferSendResult WsStreamRouter::TryPostFileTransferMessage(
        const std::shared_ptr<Data>& data) {
        if (!file_allowed_.load()) {
            return FileTransferSendResult::Disconnected(
                "WebSocket control session has no file-transfer capability");
        }
        if (!data) {
            return FileTransferSendResult::TransportError(
                "WebSocket file-transfer payload is empty");
        }
        if (!session_ || !session_->is_started()) {
            return FileTransferSendResult::Disconnected(
                "WebSocket control session is not connected");
        }
        if (GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
            return FileTransferSendResult::Busy(
                "WebSocket control queue is full",
                AcquireWritableSignal());
        }
        PostBinaryMessage(data);
        return FileTransferSendResult::Accepted();
    }

    void WsStreamRouter::SetUdpMediaFallbackCallback(std::function<void()> callback) {
        udp_media_fallback_callback_ = std::move(callback);
    }

    std::shared_ptr<FileTransferWritableSignal> WsStreamRouter::AcquireWritableSignal() {
        std::shared_ptr<FileTransferWritableSignal> signal;
        {
            std::lock_guard lock(writable_signal_mutex_);
            if (!writable_signal_
                || writable_signal_->outcome() != FileTransferWritableOutcome::kPending) {
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
}
