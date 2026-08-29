//
// Created by RGAA on 17/05/2025.
//

#ifndef PX_CT_PANEL_CLIENT_H
#define PX_CT_PANEL_CLIENT_H

#include <memory>
#include <atomic>
#include "ct_app_message.h"
#include <asio2/websocket/wss_client.hpp>

namespace px
{

    class ClientContext;
    class MessageListener;
    class PxConnectionAttemptWorkflow;

    class CtPanelClient : public std::enable_shared_from_this<CtPanelClient> {
    public:
        explicit CtPanelClient(const std::shared_ptr<ClientContext>& ctx);
        void Start();
        void Exit();

    private:
        bool IsAlive();
        void ParseMessage(std::string_view data);
        void Hello();
        void HeartBeat();
        void ReportTransportConnected();
        void ReportFileTransferBegin(const MsgClientFileTransmissionBegin& msg);
        void ReportFileTransferEnd(const MsgClientFileTransmissionEnd& msg);
        void RequestRtcIceRestart();

    private:
        std::shared_ptr<ClientContext> context_ = nullptr;
        std::shared_ptr<asio2::ws_client> client_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<PxConnectionAttemptWorkflow> connection_workflow_ = nullptr;
        std::atomic_bool exiting_ = false;
        std::atomic_bool transport_connected_ = false;
        std::atomic_bool transport_reported_ = false;
    };

}

#endif //PX_CT_PANEL_CLIENT_H
