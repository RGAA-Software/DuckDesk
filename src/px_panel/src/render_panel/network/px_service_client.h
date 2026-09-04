//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_WS_PANEL_CLIENT_H
#define PX_WS_CLIENT_H

#include <memory>
#include <string>
#include <atomic>
#include <asio2/websocket/wss_client.hpp>

namespace px
{

    class PxContext;
    class PxApplication;
    class PxStatistics;
    class MessageListener;
    class MsgAuthInfo;
    class PxAsyncScope;
    class PxReconnectSupervisor;

    class PxServiceClient : public std::enable_shared_from_this<PxServiceClient> {
    public:
        explicit PxServiceClient(const std::shared_ptr<PxApplication>& app);
        ~PxServiceClient();
        void Start();
        void Exit();
        bool IsAlive();
        void PostNetMessage(const std::string& msg);

    private:
        void HeartBeat();
        void ParseMessage(const std::string& msg);
        void SendAuthInfo();
        void FillAuthInfo(MsgAuthInfo& auth_info);

    private:
        std::shared_ptr<PxStatistics> statistics_ = nullptr;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<asio2::ws_client> client_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<PxAsyncScope> connection_scope_{};
        std::shared_ptr<PxReconnectSupervisor> reconnect_supervisor_{};
        std::atomic_int queuing_message_count_ = 0;
        std::atomic_bool exiting_ = false;
    };

}

#endif //PX_WS_PANEL_CLIENT_H
