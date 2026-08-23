//
// ft 主控端插件壳 — rustdesk 协议迁移阶段 3
// 协议状态机全部来自共享引擎库 px_ft_engine;本壳只负责:
//   - 消息收发接线(引擎回调 -> ClientPluginNetworkEvent -> PostFileTransferMessage;
//     收到 kFileAction/kFileResponse -> 入队喂引擎)
//   - 线程隔离(core 单 worker 线程,见 ft_core.h)
//   - ShowRootWidget()/HasProcessingTasks() 基类虚方法
//   - 审计事件(kPluginFileTransBeginEvent/EndEvent,对齐旧插件的 Console 记录链路)
//

#ifndef PX_CLIENT_FT_CLIENT_PLUGIN_H
#define PX_CLIENT_FT_CLIENT_PLUGIN_H

#include "px_client/plugin_interface/ct_plugin_interface.h"

#include <atomic>
#include <memory>
#include <unordered_map>

namespace px
{

    class FtCore;
    class FtWindow;

    class FtClientPlugin : public ClientPluginInterface {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;

        bool OnCreate(const px::ClientPluginParam& param) override;
        bool OnStop() override;
        bool OnDestroy() override;

        void OnMessage(std::shared_ptr<Message> msg) override;
        void ShowRootWidget() override;
        bool HasProcessingTasks() override;
        void OnTransportConnected() override;
        void SyncClientPluginSettings(const px::ClientPluginSettings& st) override;

        // 引擎发送回调(ft core 的 worker 线程调用):
        // 补 type/stream_id/device_id 后经 ClientPluginNetworkEvent 走
        // ct_plugin_event_router -> ThunderSdk::PostFileTransferMessage 的既有路径。
        // 反压:在途(已投递未完成)消息超过阈值返回 false,引擎压入内部待发队列、
        // 本 tick 不再读盘(对齐 render 壳的水位语义;通道真正繁忙时
        // PostFileTransferMessage 内部自旋等待,只阻塞本插件的 work 线程)。
        bool SendToChannel(const px::Message& msg);

    private:
        void TrackJobBegin(int32_t job_id, const QString& name, bool is_download);
        void TrackJobEnd(int32_t job_id, const QString& error_or_empty);

    private:
        FtCore* core_ = nullptr;      // QObject 父子树管理
        FtWindow* window_ = nullptr;  // root_widget_ 的子控件

        // 反压在途计数(worker 线程加,context work 线程减)
        std::atomic<int64_t> outstanding_sends_ = 0;

        // 审计配对(UI 线程)
        std::unordered_map<int32_t, QString> audit_jobs_;
    };

}

#endif //PX_CLIENT_FT_CLIENT_PLUGIN_H
