//
// Created by RGAA on 8/12/2024.
//

#include "connection.h"

namespace px {

Connection::Connection(const std::shared_ptr<MessageNotifier>& notifier) : msg_notifier_(notifier) {}

Connection::~Connection() {}

void Connection::Start() {}

void Connection::Stop() { NotifyFileTransferClosed(); }

FileTransferSendResult Connection::PostFileTransferMessage(std::shared_ptr<Data> payload) {
    if (!payload) return FileTransferSendResult::TransportError("file-transfer message is empty");
    if (!IsAlive()) return FileTransferSendResult::Disconnected("file-transfer connection is not alive");
    if (GetQueuingMsgCount() >= kMaxFileTransferQueuedMessages) {
        const auto signal = AcquireFileTransferWritableSignal();
        if (GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) signal->NotifyWritable();
        return FileTransferSendResult::Busy("file-transfer connection queue is full", signal);
    }
    PostBinaryMessage(std::move(payload));
    return FileTransferSendResult::Accepted();
}

int64_t Connection::GetQueuingMsgCount() { return queuing_message_count_; }

std::shared_ptr<FileTransferWritableSignal> Connection::AcquireFileTransferWritableSignal() {
    std::lock_guard lock(writable_signal_mutex_);
    if (!writable_signal_ || writable_signal_->outcome() != FileTransferWritableOutcome::kPending) {
        writable_signal_ = FileTransferWritableSignal::Create();
    }
    return writable_signal_;
}

void Connection::NotifyFileTransferWritable() {
    std::shared_ptr<FileTransferWritableSignal> signal;
    {
        std::lock_guard lock(writable_signal_mutex_);
        signal = std::move(writable_signal_);
    }
    if (signal) {
        signal->NotifyWritable();
    }
}

void Connection::NotifyFileTransferClosed() {
    std::shared_ptr<FileTransferWritableSignal> signal;
    {
        std::lock_guard lock(writable_signal_mutex_);
        signal = std::move(writable_signal_);
    }
    if (signal) {
        signal->Close();
    }
}

}  // namespace px
