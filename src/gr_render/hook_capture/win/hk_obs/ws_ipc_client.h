//
// Created by RGAA on 2024/3/17.
//

#ifndef TC_APPLICATION_WS_IPC_CLIENT_H
#define TC_APPLICATION_WS_IPC_CLIENT_H

#include <memory>
#include <functional>
#include <string>
#include <asio2/websocket/ws_client.hpp>

namespace tc
{

    class Message;
    class CaptureBaseMessage;

    using WsIpcMessageCallback = std::function<void(const std::shared_ptr<CaptureBaseMessage>&)>;

    // Plain WS client — must match render net_ws (asio2::http_server), not WSS.
    class WsIpcClient {
    public:

        static std::shared_ptr<WsIpcClient> Make(int port);
        explicit WsIpcClient(int port);

        void Start();
        void Exit();

        void PostIpcMessage(const std::string& msg);
        void RegisterIpcMessageCallback(WsIpcMessageCallback&& cbk) { ipc_cbk_ = std::move(cbk); }

    private:
        void DispatchIpcMessage(std::string_view data);

    private:

        int port_{0};
        std::shared_ptr<asio2::ws_client> ws_client_ = nullptr;
        WsIpcMessageCallback ipc_cbk_;
    };

}

#endif //TC_APPLICATION_WS_IPC_CLIENT_H
