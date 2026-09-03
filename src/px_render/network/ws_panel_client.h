//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_WS_PANEL_CLIENT_H
#define PX_WS_PANEL_CLIENT_H

#include <memory>
#include <string>
#include <atomic>
#include <asio2/asio2.hpp>

namespace px
{
    namespace render {
        class RenderCompositionRoot;
    }

    class Data;
    class RdContext;
    class RdStatistics;
    class MessageListener;
    class RdSettings;
    class RenderModuleRegistry;
    class PxConnectionAttemptWorkflow;

    class WsPanelClient : public std::enable_shared_from_this<WsPanelClient> {
    public:
        explicit WsPanelClient(const std::shared_ptr<RdContext>& ctx);
        ~WsPanelClient();
        void Start();
        void Exit();
        bool PostNetMessage(std::shared_ptr<Data> msg);
        bool Alive() const;

        void ReportMonitorChanged();

    private:
        void ReportStatistics();
        void SendStatisticsInternal();
        void SendPluginsInfoInternal();
        void ParseNetMessage(std::string_view msg);
        void ProcessCommandEnablePlugin(const std::string& plugin_id);
        void ProcessCommandDisablePlugin(const std::string& plugin_id);

    private:
        std::shared_ptr<RdStatistics> statistics_ = nullptr;
        RdSettings* settings_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<asio2::ws_client> client_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<MessageListener> state_msg_listener_ = nullptr;
        std::shared_ptr<PxConnectionAttemptWorkflow> connection_workflow_ = nullptr;
        std::atomic_int queuing_message_count_ = 0;
        std::shared_ptr<RenderModuleRegistry> module_registry_ = nullptr;
        std::shared_ptr<render::RenderCompositionRoot> composition_root_;
        std::atomic_bool exiting_ = false;
        // Stable across websocket auto-reconnects, changes when the render process restarts.
        std::string instance_id_;
    };

}

#endif //PX_WS_PANEL_CLIENT_H
