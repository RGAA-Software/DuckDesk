//
// Created by RGAA on 2024/3/5.
//

#include "ws_filetransfer_router.h"
#include <atomic>
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/thread_util.h"
#include "ws_plugin.h"
#include "px_message.pb.h"

namespace px
{

    void WsFileTransferRouter::OnOpen(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnOpen(sess_ptr);
        LOGI("FileTransfer OnOpen.");
    }

    void WsFileTransferRouter::OnClose(std::shared_ptr<asio2::http_session> &sess_ptr) {
        NotifyClosed();
        WsRouter::OnClose(sess_ptr);
        LOGI("FileTransfer OnClose.");
    }

    void WsFileTransferRouter::OnMessage(std::shared_ptr<asio2::http_session>& sess_ptr, int64_t socket_fd, std::string_view data) {
        WsRouter::OnMessage(sess_ptr, socket_fd, data);
        static std::atomic<uint64_t> received_count{0};
        const auto count = ++received_count;
        if (count <= 5 || (count % 500) == 0) {
            LOGI("FileTransfer receive: n={}, socket={}, bytes={}", count, socket_fd, data.size());
        }
        auto plugin = Get<WsPlugin*>("plugin");
        auto msg = Data::Make(data.data(), data.size());
        plugin->OnClientEventCameDirectly(
            true, socket_fd, NetPluginType::kWebSocket, nt_channel_type_,
            std::move(msg), binding_id_);
    }

    void WsFileTransferRouter::OnPing(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPing(sess_ptr);
    }

    void WsFileTransferRouter::OnPong(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPong(sess_ptr);
    }

    void WsFileTransferRouter::PostBinaryMessage(std::shared_ptr<Data> msg) {
        static_cast<void>(TryPostBinaryMessage(msg));
    }

    FileTransferSendResult WsFileTransferRouter::TryPostBinaryMessage(
        const std::shared_ptr<Data>& msg) {
        if (!file_allowed_.load()) {
            return FileTransferSendResult::Disconnected(
                "WebSocket file-transfer capability was revoked");
        }
        if (!msg) {
            return FileTransferSendResult::TransportError(
                "WebSocket file-transfer payload is empty");
        }
        if (!session_ || !session_->is_started()) {
            return FileTransferSendResult::Disconnected(
                "WebSocket file-transfer session is not connected");
        }
        if (GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
            return FileTransferSendResult::Busy(
                "WebSocket file-transfer queue is full",
                AcquireWritableSignal());
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
        session_->async_send(msg->CStr(), msg->Size(),
                             [weak_self, payload = msg](size_t byte_sent) {
            static_cast<void>(payload);
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            const auto remaining = --self->queuing_message_count_;
            if (remaining <= kFileTransferQueueLowWatermark) {
                self->NotifyWritable();
            }

            // report data size
            auto plugin = self->Get<WsPlugin*>("plugin");
            if (plugin) {
                plugin->ReportSentDataSize((int)byte_sent);
            }
        });
        return FileTransferSendResult::Accepted();
    }

    std::shared_ptr<FileTransferWritableSignal>
    WsFileTransferRouter::AcquireWritableSignal() {
        std::shared_ptr<FileTransferWritableSignal> signal;
        {
            std::lock_guard lock(writable_signal_mutex_);
            if (!writable_signal_ ||
                writable_signal_->outcome() != FileTransferWritableOutcome::kPending) {
                writable_signal_ = FileTransferWritableSignal::Create();
            }
            signal = writable_signal_;
        }
        if (GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
            signal->NotifyWritable();
        }
        return signal;
    }

    void WsFileTransferRouter::NotifyWritable() {
        std::shared_ptr<FileTransferWritableSignal> signal;
        {
            std::lock_guard lock(writable_signal_mutex_);
            signal = std::move(writable_signal_);
        }
        if (signal) {
            signal->NotifyWritable();
        }
    }

    void WsFileTransferRouter::NotifyClosed() {
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
