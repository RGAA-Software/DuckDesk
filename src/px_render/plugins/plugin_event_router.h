//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_PLUGIN_EVENT_ROUTER_H
#define PX_PLUGIN_EVENT_ROUTER_H

#include <memory>
#include <string>

namespace px
{

    class RdContext;
    class RdApplication;
    class RdStatistics;
    class PxPluginBaseEvent;
    class PluginManager;
    class PluginStreamEventRouter;
    class PluginNetEventRouter;
    class MessageNotifier;
    class PxPluginFileTransferBegin;
    class PxPluginFileTransferEnd;
    class PxPluginRemoteClipboardResp;
    class PxPluginPanelStreamMessage;

    class PluginEventRouter {
    public:
        explicit PluginEventRouter(const std::shared_ptr<RdApplication>& app);
        void ProcessPluginEvent(const std::shared_ptr<PxPluginBaseEvent>& event);

    private:
        void SendAnswerSdpToRemote(const std::shared_ptr<PxPluginBaseEvent>& event);
        void SendIceToRemote(const std::shared_ptr<PxPluginBaseEvent>& event);
        void ReportFileTransferBegin(const std::shared_ptr<PxPluginFileTransferBegin>& event);
        void ReportFileTransferEnd(const std::shared_ptr<PxPluginFileTransferEnd>& event);
        void ReportRemoteClipboardResp(const std::shared_ptr<PxPluginRemoteClipboardResp>& event);
        // from remote panel
        void ProcessPanelStreamMessage(const std::shared_ptr<PxPluginPanelStreamMessage>& event);
        void ReportRelayAlive(const std::string& device_id, int64_t timestamp);

    private:
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<PluginManager> plugin_manager_ = nullptr;
        std::shared_ptr<PluginStreamEventRouter> stream_event_router_ = nullptr;
        std::shared_ptr<PluginNetEventRouter> net_event_router_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        RdStatistics* stat_ = nullptr;

    };

}

#endif //PX_PLUGIN_EVENT_ROUTER_H
