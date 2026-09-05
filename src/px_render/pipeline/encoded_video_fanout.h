//
// Created by RGAA on 21/11/2024.
//

#ifndef PX_ENCODED_VIDEO_FANOUT_H
#define PX_ENCODED_VIDEO_FANOUT_H

#include <memory>
#include <atomic>
#include "px_render/architecture/events/render_event.h"

namespace px
{

    class RdContext;
    class RdApplication;
    class RenderModuleRegistry;
    class RdStatistics;
    class MessageListener;

    class EncodedVideoFanout : public std::enable_shared_from_this<EncodedVideoFanout> {
    public:
        static std::shared_ptr<EncodedVideoFanout> Make(
            const std::shared_ptr<RdApplication>& app);
        explicit EncodedVideoFanout(const std::shared_ptr<RdApplication>& app);

        void ProcessEncodedVideoFrameEvent(const std::shared_ptr<EncodedVideoFrameEvent>& event);

    private:
        void InitListener();
        std::shared_ptr<RdStatistics> statistics_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<RenderModuleRegistry> module_registry_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::atomic_bool awaiting_topology_first_frame_ = false;
    };

}

#endif //PX_ENCODED_VIDEO_FANOUT_H
