//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_RENDER_SERVICE_CLIENT_H
#define PX_RENDER_SERVICE_CLIENT_H

#include <asio2/websocket/wss_client.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "network/render_service_rpc_state.h"
#include "px_common/async_result.h"
#include "px_common/async_runtime.h"

namespace px {

class RdContext;
class RdApplication;
class RdStatistics;
class MessageListener;
class PxReconnectSupervisor;
template <typename Client>
class PxReconnectAdapterSlot;
template <typename T>
class PxAsyncMailbox;
class RenderServiceClient
    : public std::enable_shared_from_this<RenderServiceClient> {
public:
    explicit RenderServiceClient(const std::shared_ptr<RdApplication>& app);
    ~RenderServiceClient();
    void Start();
    void Exit();
    [[nodiscard]] static PxAwaitable<PxResult<void>> StopAsync(
        std::shared_ptr<RenderServiceClient> owner,
        std::chrono::steady_clock::time_point deadline);
    bool IsAlive() const;
    void PostNetMessage(const std::string& msg);
    void NotifyAppInstanceReady(const std::string& instance_id, int listen_port,
                                bool ok, const std::string& error);
    void RequestVirtualDisplay(
        const std::string& request_id, int operation, uint32_t width,
        uint32_t height, uint32_t refresh_hz,
        std::function<void(const MsgVirtualDisplayServiceResult&)>&& callback);
    PxAwaitable<PxResult<MsgVirtualDisplayServiceResult>>
    RequestVirtualDisplayAsync(std::string request_id, int operation,
                               uint32_t width, uint32_t height,
                               uint32_t refresh_hz,
                               std::chrono::steady_clock::time_point deadline);
    PxAwaitable<PxResult<MsgFrontendAdmissionServiceResult>>
    RequestFrontendAdmissionAsync(
        std::string request_id, std::string session_id, std::int64_t revision,
        std::string frontend_token,
        std::chrono::steady_clock::time_point deadline);

private:
    struct AsyncStateSnapshot final {
        std::shared_ptr<PxAsyncScope> scope{};
        std::shared_ptr<RenderServiceRpcState> rpc_state{};
        std::shared_ptr<PxReconnectSupervisor> supervisor{};
        std::shared_ptr<PxAsyncMailbox<std::string>> mailbox{};
    };

    void HeartBeat();
    void ParseMessage(const std::string& msg);
    void SendPendingAppInstanceReady();
    PxResult<void> TryPostNetMessage(const std::string& msg);
    PxResult<void> TryPostSensitiveNetMessage(std::string msg);
    void FailPendingRequests(const PxAsyncError& error);
    std::shared_ptr<PxAsyncScope> BeginStop();
    void FinishStop();
    void ScheduleDeferredExit();
    [[nodiscard]] AsyncStateSnapshot SnapshotAsyncState() const;
    static PxAwaitable<void> RunIncomingMessageLoop(
        std::weak_ptr<RenderServiceClient> weak_client,
        std::shared_ptr<PxAsyncMailbox<std::string>> mailbox);

private:
    std::shared_ptr<RdStatistics> statistics_{};
    std::shared_ptr<RdApplication> app_{};
    std::shared_ptr<RdContext> context_{};
    std::shared_ptr<PxReconnectAdapterSlot<asio2::ws_client>> adapter_slot_{};
    std::shared_ptr<MessageListener> msg_listener_{};
    std::shared_ptr<PxAsyncScope> async_scope_{};
    std::shared_ptr<RenderServiceRpcState> rpc_state_{};
    std::shared_ptr<PxReconnectSupervisor> connection_supervisor_{};
    std::shared_ptr<PxAsyncMailbox<std::string>> incoming_messages_{};
    std::atomic_bool websocket_upgraded_{false};
    std::atomic_bool started_{false};
    std::atomic_bool exiting_{false};
    std::atomic_bool deferred_exit_scheduled_{false};
    std::atomic_int queuing_message_count_{0};
    std::mutex operation_mutex_{};
    mutable std::mutex lifecycle_mutex_{};
    std::mutex ready_mtx_;
    std::string ready_instance_id_;
    std::string ready_error_;
    int ready_listen_port_{0};
    bool ready_ok_{false};
    bool ready_pending_{false};
    std::atomic_int64_t heartbeat_index_{0};
};

}  // namespace px

#endif  // PX_WS_CLIENT_H
