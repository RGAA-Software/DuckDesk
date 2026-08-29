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
#include "px_common_new/file_transfer_send_result.h"

#include <memory>
#include <unordered_map>
#include <QPointer>

namespace px
{

    class FtCore;
    class FtClientTransportState;
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

    private:
        void TrackJobBegin(int32_t job_id, const QString& name, bool is_download);
        void TrackJobEnd(int32_t job_id, const QString& error_or_empty);

    private:
        std::unique_ptr<FtCore> core_;
        QPointer<FtWindow> window_;  // root_widget_ owns the Qt child
        std::shared_ptr<FtClientTransportState> transport_state_;

        // 审计配对(UI 线程)
        std::unordered_map<int32_t, QString> audit_jobs_;
    };

}

#endif //PX_CLIENT_FT_CLIENT_PLUGIN_H
