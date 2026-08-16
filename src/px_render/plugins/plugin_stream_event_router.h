//
// Created by RGAA on 21/11/2024.
//

#ifndef PX_PLUGIN_STREAM_EVENT_ROUTER_H
#define PX_PLUGIN_STREAM_EVENT_ROUTER_H

#include <memory>
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px
{

    class RdContext;
    class RdApplication;
    class PluginManager;
    class RdStatistics;

    class PluginStreamEventRouter {
    public:
        explicit PluginStreamEventRouter(const std::shared_ptr<RdApplication>& app);

        void ProcessEncodedVideoFrameEvent(const std::shared_ptr<PxPluginEncodedVideoFrameEvent>& event);

    private:
        RdStatistics* statistics_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<PluginManager> plugin_manager_ = nullptr;
    };

}

#endif //PX_PLUGIN_STREAM_EVENT_ROUTER_H
