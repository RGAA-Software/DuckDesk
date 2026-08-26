//
// Created by RGAA on 21/11/2024.
//

#ifndef PX_PLUGIN_STREAM_EVENT_ROUTER_H
#define PX_PLUGIN_STREAM_EVENT_ROUTER_H

#include <memory>
#include <atomic>
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px
{

    class RdContext;
    class RdApplication;
    class PluginManager;
    class RdStatistics;
    class MessageListener;

    class PluginStreamEventRouter : public std::enable_shared_from_this<PluginStreamEventRouter> {
    public:
        static std::shared_ptr<PluginStreamEventRouter> Make(
            const std::shared_ptr<RdApplication>& app);
        explicit PluginStreamEventRouter(const std::shared_ptr<RdApplication>& app);

        void ProcessEncodedVideoFrameEvent(const std::shared_ptr<PxPluginEncodedVideoFrameEvent>& event);

    private:
        void InitListener();
        std::shared_ptr<RdStatistics> statistics_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<PluginManager> plugin_manager_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::atomic_bool awaiting_topology_first_frame_ = false;
    };

}

#endif //PX_PLUGIN_STREAM_EVENT_ROUTER_H
