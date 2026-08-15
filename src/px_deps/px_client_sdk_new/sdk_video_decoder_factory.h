//
// Created by RGAA on 2024/1/26.
//

#ifndef TC_CLIENT_ANDROID_VIDEO_DECODER_FACTORY_H
#define TC_CLIENT_ANDROID_VIDEO_DECODER_FACTORY_H

#include "sdk_ffmpeg_soft_decoder.h"
#include "sdk_mediacodec_video_decoder.h"

namespace tc
{

    enum SupportedCodec {
        kFFmpeg,
        kMediaCodec
    };

    class ThunderSdk;

    class VideoDecoderFactory {
    public:

        static std::shared_ptr<VideoDecoder> Make(const std::shared_ptr<ThunderSdk>& sdk, const SupportedCodec& codec) {
            if (codec == SupportedCodec::kFFmpeg) {
                return std::make_shared<FFmpegVideoDecoder>(sdk);
            }
#ifdef ANDROID
            if (codec == SupportedCodec::kMediaCodec) {
                return std::make_shared<MediacodecVideoDecoder>(sdk);
            }
#endif
            return nullptr;
        }

    };

}

#endif //TC_CLIENT_ANDROID_VIDEO_DECODER_FACTORY_H
