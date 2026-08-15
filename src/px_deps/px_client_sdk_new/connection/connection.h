//
// Created by RGAA on 8/12/2024.
//

#ifndef GAMMARAYPC_CONNECTION_H
#define GAMMARAYPC_CONNECTION_H

#include <string>
#include <functional>
#include <atomic>
#include <memory>
#include "sdk_params.h"

namespace tc
{

    class Data;
    class MessageNotifier;

    using OnConnectedCallback = std::function<void()>;
    using OnDisConnectedCallback = std::function<void()>;
    using OnMessageCallback = std::function<void(std::shared_ptr<Data>)>;

    class Connection {
    public:
        Connection(const std::shared_ptr<ThunderSdkParams>& params, const std::shared_ptr<MessageNotifier>& notifier);

        virtual ~Connection();

        void RegisterOnConnectedCallback(OnConnectedCallback&& cbk) {
            conn_cbk_ = cbk;
        }

        void RegisterOnDisConnectedCallback(OnDisConnectedCallback&& cbk) {
            dis_conn_cbk_ = cbk;
        }

        void RegisterOnMessageCallback(OnMessageCallback&& cbk) {
            msg_cbk_ = cbk;
        }

        virtual void Start();
        virtual void Stop();
        virtual void PostBinaryMessage(std::shared_ptr<Data> msg) = 0;
        virtual void PostTextMessage(const std::string& msg) {}
        virtual int64_t GetQueuingMsgCount();
        virtual void RequestPauseStream() {}
        virtual void RequestResumeStream() {}
        virtual void On16msTimeout() {}
        virtual void RetryConnection() {}
        virtual bool IsAlive() { return true; }

    protected:
        OnConnectedCallback conn_cbk_;
        OnDisConnectedCallback dis_conn_cbk_;
        OnMessageCallback msg_cbk_;
        std::atomic_int64_t queuing_message_count_ = 0;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<ThunderSdkParams> sdk_params_;
    };

}

#endif //GAMMARAYPC_CONNECTION_H
