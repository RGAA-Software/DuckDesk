//
// render 被控端文件传输插件(薄壳) — rustdesk 协议迁移阶段 2
// 协议与文件系统语义全部在共享引擎库 px_ft_engine(src/px_deps/px_ft_engine),
// 本壳只负责:消息收发接线、线程隔离、权限开关、文件数上限、审计事件上报。
//

#ifndef PX_FT_PLUGIN_H
#define PX_FT_PLUGIN_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "px_render/plugin_interface/px_plugin_interface.h"
#include "px_common_new/file_transfer_route_registry.h"

namespace px::ft
{
    class FtAsyncSession;
    class FtEngine;
}

namespace px
{
    class FileAction;
    class PxAsyncRuntime;

    class FtPlugin : public PxPluginInterface {
    public:
        ~FtPlugin() override;

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

        bool OnCreate(const px::PxPluginParam& param) override;
        bool OnStop() override;
        bool OnDestroy() override;

        void OnMessage(std::shared_ptr<Message> msg) override;
        void OnMessageRaw(const std::any& msg) override;
        void OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id) override;
        void OnSyncPluginSettingsInfo(const PxPluginSettingsInfo& settings) override;

    private:
        class AsyncBridge;

        void StopSessions();
        void RetireSession(const std::string& logical_session_id,
                           const std::string& stream_id, bool close_audits);
        std::shared_ptr<px::ft::FtAsyncSession> GetOrCreateSession(
            const std::string& logical_session_id, const std::string& stream_id);
        void ProcessMessage(const std::shared_ptr<px::ft::FtEngine>& engine,
                            const std::shared_ptr<Message>& msg,
                            const std::string& logical_session_id,
                            const std::string& source_plugin_id,
                            const std::string& source_connection_id);
        void ProcessRouteDisconnected(const std::string& logical_session_id,
                                      const std::string& stream_id,
                                      const std::string& source_plugin_id,
                                      const std::string& source_connection_id);
        void HandleOverwriteFallback(const std::string& logical_session_id,
                                     const std::string& stream_id,
                                     int32_t job_id,
                                     int32_t file_num);
        // 引擎发送回调(仅 Session state strand):补 type/stream_id/device_id 后经
        // DispatchTargetFileTransferMessage 回包。通道忙时 prepared packet 保留在
        // Engine，Session 按退避策略等待后重试。
        //
        // 线程安全性核实结论(migration plan §2 阶段2 开工项):
        // DispatchTargetFileTransferMessage 可从非分发线程直调,无需回程队列:
        //  1) net_plugins_ 仅在插件加载期(plugin_manager.cpp AttachNetPlugin)与
        //     OnDestroy(px_plugin_interface.cpp)写入,Running 期间只读,Session 并发读安全;
        //  2) 各 net 插件的 PostTargetFileTransferProtoMessage 内部本就是跨线程设计:
        //     net_rtc/net_rtc_local 显式 PostTask 到 WebRTC 网络线程再 Send,
        //     net_ws 经 ConcurrentHashMap + asio 队列投递;
        //  3) 既有先例:ws_panel_client.cpp 在 panel WS 线程直调,clipboard 在 work 线程调用。
        //  唯一约束:全部 Session 必须先于基类 OnDestroy(清空 net_plugins_)收敛。
        FileTransferSendResult SendToChannel(const std::string& logical_session_id,
                                             const px::Message& msg,
                                             const std::string& stream_id);

        // ---- 权限 / 上限 ----
        // file_transfer_enabled 关闭时在入口直接拒绝(dispatch 线程,小消息直发,
        // 对齐 rustdesk "No permission of file transfer")。
        void ReplyNoPermission(const std::shared_ptr<Message>& in_msg,
                               const std::string& source_plugin_id,
                               const std::string& source_connection_id);
        // 文件数上限预检(Session state strand),超限回 FileTransferError 且不喂引擎。
        // 返回 false 表示已拒绝。对齐 rustdesk check_file_count_limit(ui_cm_interface.rs:112)。
        bool CheckFileCountLimit(const px::FileAction& action,
                                 const std::string& logical_session_id,
                                 const std::string& stream_id);
        // 引擎 NewRead 失败(多为路径不存在)时只回 error 不触发 job_done 回调,
        // 会产生只有 Begin 没有 End 的悬挂审计记录,这里提前拦掉。
        bool CheckReadPathExists(const px::FileAction& action,
                                 const std::string& logical_session_id,
                                 const std::string& stream_id);

        // ---- 审计(panel Console 链路:kRpFileTransferBegin/End)----
        void TrackJobBegin(const std::string& logical_session_id, const std::string& stream_id,
                           int32_t job_id, const std::string& direction,
                           const std::string& path, uint64_t total_size,
                           const std::shared_ptr<Message>& msg);
        void TrackJobEnd(const std::string& logical_session_id, const std::string& stream_id,
                         int32_t job_id, const std::string& error_or_empty);
        // 关闭指定逻辑会话/连接的悬挂审计；无 logical_session_id 时关闭全部（插件停止时）。
        void CloseAudits(const std::optional<std::string>& logical_session_id,
                         const std::string& stream_id, bool success);
        [[nodiscard]] static std::string MakeSessionKey(
            const std::string& logical_session_id, const std::string& stream_id);

    private:
        std::shared_ptr<PxAsyncRuntime> async_runtime_;
        std::shared_ptr<AsyncBridge> async_bridge_;
        std::mutex route_session_mutex_;
        std::mutex sessions_mutex_;
        std::unordered_map<std::string, std::shared_ptr<px::ft::FtAsyncSession>> sessions_;
        FileTransferRouteRegistry routes_;
        std::atomic_bool accepting_ = false;

        struct AuditRecord {
            std::string the_file_id_;
            int64_t begin_timestamp_ = 0;
            std::string logical_session_id_;
            std::string stream_id_; // 归属连接,断线按连接闭环审计
        };
        std::unordered_map<std::string, AuditRecord> audits_;
    };

}

#endif //PX_FT_PLUGIN_H
