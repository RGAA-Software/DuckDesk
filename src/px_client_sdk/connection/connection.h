//
// Created by RGAA on 8/12/2024.
//

#ifndef PIXELSPC_CONNECTION_H
#define PIXELSPC_CONNECTION_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "px_common/file_transfer_send_result.h"

namespace px
{

    class Data;
    class MessageNotifier;

    using OnConnectedCallback = std::function<void()>;
    using OnDisConnectedCallback = std::function<void()>;
    using OnMessageCallback = std::function<void(std::shared_ptr<Data>)>;

    class Connection {
    public:
        explicit Connection(const std::shared_ptr<MessageNotifier>& notifier);

        virtual ~Connection();

        void RegisterOnConnectedCallback(OnConnectedCallback&& callback) {
            conn_cbk_ = std::move(callback);
        }

        void RegisterOnDisConnectedCallback(OnDisConnectedCallback&& callback) {
            dis_conn_cbk_ = std::move(callback);
        }

        void RegisterOnMessageCallback(OnMessageCallback&& callback) {
            msg_cbk_ = std::move(callback);
        }

        virtual void Start();
        virtual void Stop();
        virtual void PostBinaryMessage(std::shared_ptr<Data> payload) = 0;
        // Reliable protocol streams require a real write completion. Unsupported transports fail explicitly.
        virtual void PostReliableBinaryMessage(std::shared_ptr<Data>, std::function<void(bool)> completion) {
            if (completion) {
                completion(false);
            }
        }
        virtual void PostTextMessage(const std::string& message) {
            static_cast<void>(message);
        }
        virtual int64_t GetQueuingMsgCount();
        virtual void RequestPauseStream() {}
        virtual void RequestResumeStream() {}
        virtual void On16msTimeout() {}
        virtual bool IsAlive() { return true; }
        [[nodiscard]] virtual std::shared_ptr<FileTransferWritableSignal>
        AcquireFileTransferWritableSignal();

    protected:
        void NotifyFileTransferWritable();
        void NotifyFileTransferClosed();

        OnConnectedCallback conn_cbk_;
        OnDisConnectedCallback dis_conn_cbk_;
        OnMessageCallback msg_cbk_;
        std::atomic_int64_t queuing_message_count_ = 0;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::mutex writable_signal_mutex_;
        std::shared_ptr<FileTransferWritableSignal> writable_signal_;
    };

}

#endif //PIXELSPC_CONNECTION_H
