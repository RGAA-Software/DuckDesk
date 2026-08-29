//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_RENDER_SERVICE_CLIENT_H
#define PX_RENDER_SERVICE_CLIENT_H

#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <asio2/websocket/wss_client.hpp>

#include "px_common_new/async_result.h"
#include "px_common_new/async_runtime.h"
#include "network/render_service_rpc_state.h"

namespace px
{

    class RdContext;
    class RdApplication;
    class RdStatistics;
    class MessageListener;
    class PxConnectionAttemptWorkflow;
    class RenderServiceClient : public std::enable_shared_from_this<RenderServiceClient> {
    public:

        explicit RenderServiceClient(const std::shared_ptr<RdApplication>& app);
        ~RenderServiceClient();
        void Start();
        void Exit();
        bool IsAlive() const;
        void PostNetMessage(const std::string& msg);
        void NotifyAppInstanceReady(const std::string& instance_id, int listen_port,
                                    bool ok, const std::string& error);
        void RedeemConnectionTicket(
            const std::string& ticket,
            const std::string& client_nonce,
            const std::string& instance_id,
            std::function<void(bool, const std::string&, const std::vector<std::string>&,
                               const std::string&)>&& callback);
        PxAwaitable<PxResult<RedeemedConnectionTicket>> RedeemConnectionTicketAsync(
            std::string ticket,
            std::string client_nonce,
            std::string instance_id,
            std::chrono::steady_clock::time_point deadline);
        void RequestVirtualDisplay(
            const std::string& request_id,
            int operation,
            uint32_t width,
            uint32_t height,
            uint32_t refresh_hz,
            std::function<void(const MsgVirtualDisplayServiceResult&)>&& callback);
        PxAwaitable<PxResult<MsgVirtualDisplayServiceResult>> RequestVirtualDisplayAsync(
            std::string request_id,
            int operation,
            uint32_t width,
            uint32_t height,
            uint32_t refresh_hz,
            std::chrono::steady_clock::time_point deadline);

    private:
        void HeartBeat();
        void ParseMessage(const std::string& msg);
        void SendPendingAppInstanceReady();
        PxResult<void> TryPostNetMessage(const std::string& msg);
        void FailPendingRequests(const PxAsyncError& error);

    private:
        std::shared_ptr<RdStatistics> statistics_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<asio2::ws_client> client_ = nullptr;
        std::atomic_bool websocket_upgraded_ = false;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<PxAsyncScope> async_scope_ = nullptr;
        std::shared_ptr<RenderServiceRpcState> rpc_state_ = nullptr;
        std::shared_ptr<PxConnectionAttemptWorkflow> connection_workflow_ = nullptr;
        std::atomic_bool exiting_ = false;
        std::atomic_int queuing_message_count_ = 0;
        std::mutex ready_mtx_;
        std::string ready_instance_id_;
        std::string ready_error_;
        int ready_listen_port_ = 0;
        bool ready_ok_ = false;
        bool ready_pending_ = false;
    };

}

#endif //PX_WS_CLIENT_H
