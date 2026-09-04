//
// Created by RGAA on 2024/3/17.
//

#ifndef TC_APPLICATION_WS_IPC_CLIENT_H
#define TC_APPLICATION_WS_IPC_CLIENT_H

#include <memory>
#include <functional>
#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <asio2/websocket/ws_client.hpp>

#include "px_common_new/async_runtime.h"

namespace px
{

    class Message;
    class CaptureBaseMessage;
    class PxConnectionAttemptWorkflow;
    class PxReconnectBackoff;
    struct PxConnectionAttemptTicket;
    template<typename T>
    class PxAsyncMailbox;

    using WsIpcMessageCallback = std::function<void(const std::shared_ptr<CaptureBaseMessage>&)>;

    // Plain WS client — must match render net_ws (asio2::http_server), not WSS.
    class WsIpcClient : public std::enable_shared_from_this<WsIpcClient> {
    public:

        static std::shared_ptr<WsIpcClient> Make(int port);
        explicit WsIpcClient(int port);
        ~WsIpcClient();

        void Start();
        void Exit();

        void PostIpcMessage(const std::string& msg);
        void RegisterIpcMessageCallback(WsIpcMessageCallback&& cbk);

    private:
        void DispatchIpcMessage(const std::string& data);
        static PxAwaitable<void> RunIncomingMessageLoop(
            std::weak_ptr<WsIpcClient> weak_client,
            std::shared_ptr<PxAsyncMailbox<std::string>> mailbox);
        static PxAwaitable<void> RunConnectionLoop(
            std::weak_ptr<WsIpcClient> weak_client,
            std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
            std::shared_ptr<PxReconnectBackoff> backoff,
            std::shared_ptr<asio2::ws_client> client,
            int port,
            std::string path);

    private:

        int port_{0};
        std::shared_ptr<asio2::ws_client> ws_client_{};
        std::shared_ptr<PxAsyncRuntime> async_runtime_{};
        std::shared_ptr<PxAsyncScope> async_scope_{};
        std::shared_ptr<PxConnectionAttemptWorkflow> connection_workflow_{};
        std::shared_ptr<PxReconnectBackoff> connection_backoff_{};
        std::shared_ptr<PxAsyncMailbox<std::string>> incoming_messages_{};
        mutable std::mutex callback_mutex_;
        WsIpcMessageCallback ipc_cbk_{};
        std::atomic_uint64_t connection_generation_{0};
        std::atomic_uint64_t null_drop_count_{0};
        std::atomic_uint64_t stopped_drop_count_{0};
        std::atomic_bool started_{false};
        std::atomic_bool exiting_{false};
    };

}

#endif //TC_APPLICATION_WS_IPC_CLIENT_H
