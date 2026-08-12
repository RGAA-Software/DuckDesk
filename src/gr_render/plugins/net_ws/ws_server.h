//
// Created by RGAA on 2024/3/1.
//

#ifndef TC_APPLICATION_APP_SERVER_H
#define TC_APPLICATION_APP_SERVER_H

#include <memory>
#include <atomic>
#include <vector>
#include <mutex>
#include <set>
#include "network/ws_router.h"
#include "tc_common_new/concurrent_hashmap.h"
#include <asio2/asio2.hpp>

namespace tc
{
    class WsStreamRouter;
    class WsFileTransferRouter;
    class WsUserProxyRouter;
    class HttpHandler;
    class WsPlugin;
    class GrConnectedClientInfo;
    class MsgClientHello;

    class WsPluginServer : public std::enable_shared_from_this<WsPluginServer> {
    public:

        explicit WsPluginServer(tc::WsPlugin* plugin, uint16_t listen_port);

        void Start();
        void Exit();

        void PostNetMessage(std::shared_ptr<Data> msg);
        void PostIpcBinaryMessage(std::shared_ptr<Data> msg);
        bool PostTargetStreamMessage(const std::string& stream_id, std::shared_ptr<Data> msg);
        bool PostTargetFileTransferMessage(const std::string& stream_id, std::shared_ptr<Data> msg);
        int GetConnectedClientsCount();
        bool IsOnlyAudioClients();
        bool IsWorking();
        int64_t GetQueuingMediaMsgCount();
        int64_t GetQueuingFtMsgCount();
        std::vector<std::shared_ptr<GrConnectedClientInfo>> GetConnectedClientInfo();
        void OnClientHello(const std::shared_ptr<MsgClientHello>& event);

        void PostUserProxyMessage(std::shared_ptr<Data> msg);
        bool IsUserProxyConnected();

        // Game-hook /ipc 鉴权：只允许注册过的游戏 pid 连接(PrepareGameHookBoot 时注册)。
        // 游戏重启后 render 会为新的 pid 重写 boot 配置并注册,旧 render 的残留游戏
        // 不在集合里,连接会被拒绝(防多实例串帧)。
        void RegisterIpcPid(uint32_t pid);
        bool IsIpcPidAllowed(uint32_t pid);
        // 定期清扫:注销进程已死亡的 pid(断线未触发/异常退出的兜底清理),
        // 由 WsPlugin::On1Second 驱动,内部节流
        void SweepDeadIpcPids();

    private:
        // pid 对应进程已死才从允许集合注销(活进程的瞬时断线不影响重连)
        void UnregisterIpcPidIfDead(uint32_t pid);
        void AddUserProxyRouter();
        void AddIpcRouter();
        void AddWebsocketRouter(const std::string& path);
        void AddWebClientRouter();

        void AddHttpRouter(const std::string& path,
                           std::function<void(const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep)>&& callback);

        void NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id);
        void NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp);

    private:
        tc::WsPlugin* plugin_ = nullptr;
        uint16_t listen_port_ = 0;
        //std::shared_ptr<asio2::https_server> server_ = nullptr;
        std::shared_ptr<asio2::http_server> server_ = nullptr;

        WsDataPtr ws_data_ = nullptr;
        tc::ConcurrentHashMap<uint64_t, std::shared_ptr<WsStreamRouter>> stream_routers_;
        tc::ConcurrentHashMap<uint64_t, std::shared_ptr<WsFileTransferRouter>> ft_routers_;
        // Injected tc_graphics.dll sessions on /ipc (host → game input downlink).
        tc::ConcurrentHashMap<uint64_t, std::shared_ptr<asio2::http_session>> ipc_sessions_;
        // Pids allowed on /ipc: this render instance wrote hook boot config for them.
        std::mutex ipc_pid_mtx_;
        std::set<uint32_t> ipc_allowed_pids_;
        // /ipc session fd → 认证通过的游戏 pid,断线时据此做死进程注销
        tc::ConcurrentHashMap<uint64_t, uint32_t> ipc_session_pids_;
        // SweepDeadIpcPids 节流计数(On1Second 每秒调用,每 5 次真正扫一次)
        uint64_t ipc_pid_sweep_ticks_ = 0;

        std::shared_ptr<HttpHandler> http_handler_ = nullptr;
        std::shared_ptr<WsUserProxyRouter> user_proxy_router_ = nullptr;
        std::atomic_bool exiting_ = false;

    };
}

#endif //TC_APPLICATION_APP_SERVER_H
