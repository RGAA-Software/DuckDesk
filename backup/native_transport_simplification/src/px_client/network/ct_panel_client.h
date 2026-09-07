//
// Created by RGAA on 17/05/2025.
//

#ifndef PX_CT_PANEL_CLIENT_H
#define PX_CT_PANEL_CLIENT_H

#include <memory>
#include <atomic>
#include <mutex>
#include "ct_app_message.h"
#include "px_common/async_runtime.h"
#include <asio2/websocket/wss_client.hpp>

namespace px
{

    class ClientContext;
    class MessageListener;
    class PxAsyncScope;
    class PxAsyncRuntime;
    class PxReconnectSupervisor;

    class CtPanelClient : public std::enable_shared_from_this<CtPanelClient> {
    public:
        explicit CtPanelClient(const std::shared_ptr<ClientContext>& ctx);
        ~CtPanelClient();
        void Start();
        void Exit();

    private:
        bool IsAlive();
        [[nodiscard]] std::shared_ptr<asio2::ws_client> ClientSnapshot() const;
        void ParseMessage(std::string_view data);
        void Hello();
        void HeartBeat();
        void ReportTransportConnected();
        void ReportTransportRejected();
        void ReportFileTransferBegin(const MsgClientFileTransmissionBegin& msg);
        void ReportFileTransferEnd(const MsgClientFileTransmissionEnd& msg);
        void RequestRtcIceRestart();
        static PxAwaitable<void> RunHeartbeatLoop(std::weak_ptr<CtPanelClient> weak_client);
        void ScheduleDeferredExit();

    private:
        std::shared_ptr<ClientContext> context_ = nullptr;
        std::shared_ptr<asio2::ws_client> client_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<PxAsyncRuntime> async_runtime_{};
        std::shared_ptr<PxAsyncScope> connection_scope_{};
        std::shared_ptr<PxReconnectSupervisor> reconnect_supervisor_{};
        std::atomic_bool exiting_ = false;
        std::atomic_bool transport_connected_ = false;
        std::atomic_bool transport_reported_ = false;
        std::atomic_int transport_rejection_ = 0;
        std::atomic_bool deferred_exit_scheduled_{false};
        mutable std::mutex network_mutex_{};
        std::mutex operation_mutex_{};
    };

}

#endif //PX_CT_PANEL_CLIENT_H
