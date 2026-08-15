//
// Created by RGAA on 2023/12/19.
//

#ifndef TC_APPLICATION_VIDEO_ENCODER_FACTORY_H
#define TC_APPLICATION_VIDEO_ENCODER_FACTORY_H

#include <memory>
#include <string>
#include <functional>
#include "encoder_config.h"

namespace tc
{

    class VideoEncoder;
    class MessageNotifier;

    enum class ECreateEncoderPolicy {
        kAuto,
        kSpecify,
    };

    enum class ECreateEncoderName {
        kUnknownEncoder,
        kNVENC,
        kAMF,
        kFFmpeg,
    };

    class VideoEncoderFactory {
    public:
        static std::shared_ptr<VideoEncoder> CreateEncoder(
                const std::shared_ptr<MessageNotifier>& msg_notifier,
                EncoderFeature feature, ECreateEncoderPolicy policy,
                const EncoderConfig& config,
                ECreateEncoderName name = ECreateEncoderName::kUnknownEncoder
        );
    };

}

#endif //TC_APPLICATION_VIDEO_ENCODER_FACTORY_H
