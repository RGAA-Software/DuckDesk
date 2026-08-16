//
// Created by RGAA on 10/07/2025.
//

#ifndef PX_STREAM_MESSAGES_H
#define PX_STREAM_MESSAGES_H

#include <string>
#include <memory>

namespace px_cms
{
    // stream
    class CmsStream;
}

// send from panel -> remote render
// 1. direct: http request -> remote render
// 2. relay: http reqeust -> relay server -> remote render
namespace px
{

    // type
    enum class PxStreamMessageType {
       kRestartRender,
       kLockScreen,
       kRestartDevice,
       kShutdownDevice,
    };

    // base
    class PxBaseStreamMessage {
    public:
        virtual std::string AsJson() = 0;
    public:
        PxStreamMessageType type_;
        std::shared_ptr<px_cms::CmsStream> stream_item_ = nullptr;
    };

    //
    class PxSmRestartRender : public PxBaseStreamMessage {
    public:
        PxSmRestartRender() {
            type_ = PxStreamMessageType::kRestartRender;
        }

        std::string AsJson() override;
    };

    //
    class PxSmLockScreen : public PxBaseStreamMessage {
    public:
        PxSmLockScreen() {
            type_ = PxStreamMessageType::kLockScreen;
        }

        std::string AsJson() override;
    };

    //
    class PxSmRestartDevice : public PxBaseStreamMessage {
    public:
        PxSmRestartDevice() {
            type_ = PxStreamMessageType::kRestartDevice;
        }

        std::string AsJson() override;
    };

    //
    class PxSmShutdownDevice : public PxBaseStreamMessage {
    public:
        PxSmShutdownDevice() {
            type_ = PxStreamMessageType::kShutdownDevice;
        }

        std::string AsJson() override;
    };
}

#endif //PX_STREAM_MESSAGES_H
