//
// Created by RGAA on 21/11/2024.
//

#ifndef PX_ENCODED_VIDEO_FANOUT_H
#define PX_ENCODED_VIDEO_FANOUT_H

#include <memory>
#include <atomic>
#include "px_render/architecture/events/render_event.h"
#include "px_client_sdk/media_transport/video_backlog.h"

namespace px
{

    class PxContext;
    class PxApplication;
    class RenderModuleRegistry;
    class PxStatistics;
    class MessageListener;
    class Thread;

    class EncodedVideoFanout : public std::enable_shared_from_this<EncodedVideoFanout> {
    public:
        static std::shared_ptr<EncodedVideoFanout> Make(
            const std::shared_ptr<PxApplication>& app);
        explicit EncodedVideoFanout(const std::shared_ptr<PxApplication>& app);
        ~EncodedVideoFanout();

        void ProcessEncodedVideoFrameEvent(const std::shared_ptr<EncodedVideoFrameEvent>& event);

    private:
        void InitListener();
        void DrainVideo();
        media::VideoBacklog<std::shared_ptr<EncodedVideoFrameEvent>> video_backlog_{};
        std::shared_ptr<Thread> video_dispatch_{};
        std::shared_ptr<PxStatistics> statistics_ = nullptr;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<RenderModuleRegistry> module_registry_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::atomic_bool awaiting_topology_first_frame_ = false;
    };

}

#endif //PX_ENCODED_VIDEO_FANOUT_H
