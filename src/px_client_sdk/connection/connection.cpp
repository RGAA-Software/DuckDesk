//
// Created by RGAA on 8/12/2024.
//

#include "connection.h"

namespace px
{

    Connection::Connection(const std::shared_ptr<ThunderSdkParams>& params, const std::shared_ptr<MessageNotifier>& notifier) {
        sdk_params_ = params;
        msg_notifier_ = notifier;
    }

    Connection::~Connection() {

    }

    void Connection::Start() {

    }

    void Connection::Stop() {
        NotifyFileTransferClosed();
    }

    int64_t Connection::GetQueuingMsgCount() {
        return queuing_message_count_;
    }

    std::shared_ptr<FileTransferWritableSignal>
    Connection::AcquireFileTransferWritableSignal() {
        std::lock_guard lock(writable_signal_mutex_);
        if (!writable_signal_ ||
            writable_signal_->outcome() != FileTransferWritableOutcome::kPending) {
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

}
