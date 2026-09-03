//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_EVENT_INGRESS_H
#define PX_RENDER_EVENT_INGRESS_H

#include <memory>
#include <string>

namespace px
{

    class RdContext;
    class RdApplication;
    class RdStatistics;
    class PxPluginBaseEvent;
    class RenderModuleRegistry;
    class NetworkEventIngress;
    class MessageNotifier;
    class PxPluginRemoteClipboardResp;
    class PxPluginPanelStreamMessage;

    class RenderEventIngress : public std::enable_shared_from_this<RenderEventIngress> {
    public:
        explicit RenderEventIngress(const std::shared_ptr<RdApplication>& app);
        void ProcessCompatibilityEvent(const std::shared_ptr<PxPluginBaseEvent>& event);

    private:
        void SendAnswerSdpToRemote(const std::shared_ptr<PxPluginBaseEvent>& event);
        void SendIceToRemote(const std::shared_ptr<PxPluginBaseEvent>& event);
        void ReportRemoteClipboardResp(const std::shared_ptr<PxPluginRemoteClipboardResp>& event);
        // from remote panel
        void ProcessPanelStreamMessage(const std::shared_ptr<PxPluginPanelStreamMessage>& event);
        void ReportRelayAlive(const std::string& device_id, int64_t timestamp);

    private:
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<RenderModuleRegistry> module_registry_ = nullptr;
        std::shared_ptr<NetworkEventIngress> network_ingress_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<RdStatistics> stat_ = nullptr;

    };

}

#endif //PX_RENDER_EVENT_INGRESS_H
