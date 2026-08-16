//
// render 被控端文件传输插件(薄壳) — rustdesk 协议迁移阶段 2
// 协议与文件系统语义全部在共享引擎库 px_ft_engine(src/px_deps/px_ft_engine),
// 本壳只负责:消息收发接线、线程隔离、权限开关、文件数上限、审计事件上报。
//

#ifndef PX_FT_PLUGIN_H
#define PX_FT_PLUGIN_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px::ft
{
    class FtEngine;
}

namespace px
{
    class FileAction;

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
        void OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id) override;
        void OnSyncPluginSettingsInfo(const PxPluginSettingsInfo& settings) override;

    private:
        // ---- worker 线程 ----
        void WorkerMain();
        void StopWorker();
        void ProcessMessage(const std::shared_ptr<Message>& msg);
        // 引擎发送回调(仅 worker 线程):补 type/stream_id/device_id 后经
        // DispatchTargetFileTransferMessage 回包。返回 false 表示通道忙(水位满),
        // 引擎会把消息压入内部待发队列,本 tick 不再读盘。
        //
        // 线程安全性核实结论(migration plan §2 阶段2 开工项):
        // DispatchTargetFileTransferMessage 可从非分发线程直调,无需回程队列:
        //  1) net_plugins_ 仅在插件加载期(plugin_manager.cpp AttachNetPlugin)与
        //     OnDestroy(px_plugin_interface.cpp)写入,Running 期间只读,worker 并发读安全;
        //  2) 各 net 插件的 PostTargetFileTransferProtoMessage 内部本就是跨线程设计:
        //     net_rtc/net_rtc_local 显式 PostTask 到 WebRTC 网络线程再 Send,
        //     net_ws 经 ConcurrentHashMap + asio 队列投递;
        //  3) 既有先例:ws_panel_client.cpp 在 panel WS 线程直调,clipboard 在 work 线程调用。
        //  唯一约束:worker 必须先于基类 OnDestroy(清空 net_plugins_)join,见 StopWorker。
        bool SendToChannel(const px::Message& msg);

        // ---- 权限 / 上限 ----
        // file_transfer_enabled 关闭时在入口直接拒绝(dispatch 线程,小消息直发,
        // 对齐 rustdesk "No permission of file transfer")。
        void ReplyNoPermission(const std::shared_ptr<Message>& in_msg);
        // 文件数上限预检(worker 线程),超限回 FileTransferError 且不喂引擎。
        // 返回 false 表示已拒绝。对齐 rustdesk check_file_count_limit(ui_cm_interface.rs:112)。
        bool CheckFileCountLimit(const px::FileAction& action);
        // 引擎 NewRead 失败(多为路径不存在)时只回 error 不触发 job_done 回调,
        // 会产生只有 Begin 没有 End 的悬挂审计记录,这里提前拦掉。
        bool CheckReadPathExists(const px::FileAction& action);

        // ---- 审计(panel CMS 链路:kRpFileTransferBegin/End)----
        void TrackJobBegin(int32_t job_id, const std::string& direction,
                           const std::string& path, uint64_t total_size,
                           const std::shared_ptr<Message>& msg);
        void TrackJobEnd(int32_t job_id, const std::string& error_or_empty);
        // 关闭指定连接的悬挂审计;stream_id 为空 = 全部(插件停止时)
        void CloseAudits(const std::string& stream_id, bool success);

    private:
        std::unique_ptr<px::ft::FtEngine> engine_;

        // 任务队列:dispatch 线程只入队,worker 线程唯一消费者。
        // 跨线程共享只有该队列本身(引擎全部可变状态仅 worker 持有)。
        std::mutex task_mutex_;
        std::condition_variable task_cv_;
        std::deque<std::function<void()>> tasks_;
        bool worker_exit_ = false;
        std::thread worker_;
        std::atomic_bool accepting_ = false;

        // ---- 以下仅 worker 线程访问 ----
        std::string current_stream_id_;
        // 最近活动时间:有作业或刚有活动时 1ms tick,空闲 30s(对齐 rustdesk MILLI1/SEC30)。
        // 引擎待发队列在通道忙时会积压握手消息,靠宽限期保证无作业时也能及时冲刷。
        std::chrono::steady_clock::time_point last_activity_;

        struct AuditRecord {
            std::string the_file_id_;
            int64_t begin_timestamp_ = 0;
            std::string stream_id_; // 归属连接,断线按连接闭环审计
        };
        std::unordered_map<int32_t, AuditRecord> audits_;
    };

}

#endif //PX_FT_PLUGIN_H
