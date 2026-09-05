//
// Created by RGAA on 2024/3/1.
//

#ifndef TC_APPLICATION_APP_SERVER_H
#define TC_APPLICATION_APP_SERVER_H

#include <chrono>
#include <memory>
#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>
#include <set>
#include <unordered_map>
#include "network/ws_router.h"
#include "px_common/concurrent_hashmap.h"
#include "px_common/file_transfer_send_result.h"
#include "px_common/async_result.h"
#include "px_common/async_runtime.h"
#include "diagnostics/rate_limited_log.h"
#include "diagnostics/transport_performance_window.h"
#include <asio2/asio2.hpp>

namespace px {
class WsStreamRouter;
class WsFileTransferRouter;
class WsUserProxyRouter;
class HttpHandler;
class WsTransport;
class PxConnectedClientInfo;
class MsgClientHello;
class PxLogicalSessionCapabilityUpdate;
class PxAsyncRuntime;
class PxAsyncScope;
struct WsTicketAdmission;
struct LogicalSessionAdmission;

// Lifetime:
// - Owned by the built-in WsTransport.
// - Control workflows are owned by async_scope_.
// - Network and legacy event callbacks capture weak owners only.
// - Stop closes HTTP ingress before cancelling and draining workflows.
//
// Threading:
// - Control workflows are serialized on the control-lane strand.
// - Session mutations are posted back to the asio2 session queue.
// - No mutex or borrowed request/response value crosses co_await.
class WsServer : public std::enable_shared_from_this<WsServer> {
  public:
    explicit WsServer(std::weak_ptr<WsTransport> transport, std::shared_ptr<PxAsyncRuntime> async_runtime, uint16_t listen_port);

    [[nodiscard]] bool Start();
    void Exit();
    [[nodiscard]] static PxAwaitable<PxResult<void>> StopAsync(std::shared_ptr<WsServer> owner, std::chrono::steady_clock::time_point deadline);

    void PostNetMessage(std::shared_ptr<Data> msg);
    void PostIpcBinaryMessage(std::shared_ptr<Data> msg);
    bool PostTargetStreamMessage(const std::string& stream_id, std::shared_ptr<Data> msg);
    FileTransferSendResult PostTargetFileTransferMessage(const std::string& stream_id, const std::shared_ptr<Data>& msg,
                                                         const std::string& connection_instance_id = {});
    int GetConnectedClientsCount();
    bool IsOnlyAudioClients();
    bool IsWorking();
    int64_t GetQueuingMediaMsgCount();
    int64_t GetQueuingFtMsgCount();
    std::vector<std::shared_ptr<PxConnectedClientInfo>> GetConnectedClientInfo();
    void OnClientHello(const std::shared_ptr<MsgClientHello>& event);
    void UpdateLogicalSessionCapabilities(const PxLogicalSessionCapabilityUpdate& update);

    void PostUserProxyMessage(std::shared_ptr<Data> msg);
    bool IsUserProxyConnected();

    // Game-hook /ipc 鉴权：只允许注册过的游戏 pid 连接(PrepareGameHookBoot 时注册)。
    // 游戏重启后 render 会为新的 pid 重写 boot 配置并注册,旧 render 的残留游戏
    // 不在集合里,连接会被拒绝(防多实例串帧)。
    void RegisterIpcPid(uint32_t pid);
    bool IsIpcPidAllowed(uint32_t pid);
    // 定期清扫:注销进程已死亡的 pid(断线未触发/异常退出的兜底清理),
    // 由 WsTransport::On1Second 驱动,内部节流
    void SweepDeadIpcPids();
    void ReportPerformance();

  private:
    std::shared_ptr<PxAsyncScope> BeginStop();
    void FinishStop();
    // pid 对应进程已死才从允许集合注销(活进程的瞬时断线不影响重连)
    void UnregisterIpcPidIfDead(uint32_t pid);
    void AddUserProxyRouter();
    void AddIpcRouter();
    void AddWebsocketRouter(const std::string& path);
    void AddWebClientRouter();
    static PxAwaitable<void> OpenWebSocketAsync(std::weak_ptr<WsServer> owner, std::shared_ptr<asio2::http_session> session, std::string path,
                                                std::unordered_map<std::string, std::string> params, std::uint64_t socket_fd);
    void FinalizeWebSocketOpen(const std::shared_ptr<asio2::http_session>& session, const std::string& path,
                               const std::unordered_map<std::string, std::string>& params, const WsTicketAdmission& ticket,
                               const LogicalSessionAdmission& admission, const std::string& binding_id, std::uint64_t socket_fd);

    void AddHttpRouter(const std::string& path, std::function<void(const std::string& path, std::shared_ptr<asio2::http_session>& session_ptr,
                                                                   http::web_request& req, http::web_response& rep)>&& callback);

    void NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id);
    void NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id,
                                       int64_t begin_timestamp, const std::string& connection_instance_id = {},
                                       const std::string& logical_session_id = {});
    void CloseLogicalSessionBinding(const std::string& logical_session_id, const std::string& binding_id);
    void UpdateUdpMediaAssociation(const std::string& association_code, const std::string& logical_session_id, const std::string& stream_id,
                                   bool force_gdi, bool revoke);

  private:
    // Weak observer: WsTransport owns this server and must not form a cycle.
    std::weak_ptr<WsTransport> transport_;
    uint16_t listen_port_ = 0;
    // std::shared_ptr<asio2::https_server> server_ = nullptr;
    std::shared_ptr<asio2::http_server> server_{};

    WsDataPtr ws_data_{};
    px::ConcurrentHashMap<uint64_t, std::shared_ptr<WsStreamRouter>> stream_routers_;
    px::ConcurrentHashMap<uint64_t, std::shared_ptr<WsFileTransferRouter>> ft_routers_;
    // Injected px_gh.dll sessions on /ipc (host → game input downlink).
    px::ConcurrentHashMap<uint64_t, std::shared_ptr<asio2::http_session>> ipc_sessions_;
    // Pids allowed on /ipc: this render instance wrote hook boot config for them.
    std::mutex ipc_pid_mtx_;
    std::set<uint32_t> ipc_allowed_pids_;
    // /ipc session fd → 认证通过的游戏 pid,断线时据此做死进程注销
    px::ConcurrentHashMap<uint64_t, uint32_t> ipc_session_pids_;
    // SweepDeadIpcPids 节流计数(On1Second 每秒调用,每 5 次真正扫一次)
    uint64_t ipc_pid_sweep_ticks_ = 0;

    std::shared_ptr<HttpHandler> http_handler_{};
    std::shared_ptr<WsUserProxyRouter> user_proxy_router_{};
    // The runtime is injected by the Render composition root. This server owns only its cancellable scope.
    std::shared_ptr<PxAsyncRuntime> async_runtime_{};
    std::shared_ptr<PxAsyncScope> async_scope_{};
    std::atomic_bool exiting_{false};
    render::TransportPerformanceWindow transport_performance_;
    render::RateLimitedLogGate warning_log_gate_{std::chrono::seconds(5), 64};
};
} // namespace px

#endif // TC_APPLICATION_APP_SERVER_H
