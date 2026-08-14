//
// Created by RGAA on 2023/12/19.
//

#include "video_encoder_factory.h"
#include "ffmpeg_video_encoder.h"
#include "nvenc_video_encoder.h"
#include "tc_common_new/log.h"
#include "video_encoder.h"
#include "tc_encoder_new/legacy/VideoEncoderVCE.h"

#include <memory>

namespace tc
{
	std::shared_ptr<VideoEncoder> VideoEncoderFactory::CreateEncoder(const std::shared_ptr<MessageNotifier>& msg_notifier,
                                                                     EncoderFeature feature,
                                                                     ECreateEncoderPolicy policy,
                                                                     const EncoderConfig& config,
                                                                     ECreateEncoderName name)
	{
        auto fn_create_ffmpeg = [=]() -> std::shared_ptr<VideoEncoder> {
            LOGI("Finally, select FFmpeg as encoder.");
            auto encoder = std::make_shared<FFmpegVideoEncoder>(msg_notifier, feature);
            if(!encoder->Initialize(config)) {
                encoder->Exit();
                LOGE("FFmpegVideoEncoder Initialize error!");
                return nullptr;
            }
            return encoder;
        };

        auto fn_create_nvenc = [=]() -> std::shared_ptr<VideoEncoder> {
            auto encoder = std::make_shared<NVENCVideoEncoder>(msg_notifier, feature);
            if(!encoder->Initialize(config)) {
                encoder->Exit();
                LOGE("NVENCVideoEncoder Initialize error!");
                return nullptr;
            }
            return encoder;
        };

        auto fn_create_amf = [=]() -> std::shared_ptr<VideoEncoder> {
            auto encoder = std::make_shared<VideoEncoderVCE>(msg_notifier, feature);
            if (!encoder->Initialize(config)) {
                encoder->Exit();
                LOGE("AMF encoder Initialize error!");
                return nullptr;
            }
            return encoder;
        };

	    if (policy == ECreateEncoderPolicy::kAuto) {
            auto encoder = fn_create_nvenc();
            if (!encoder) {
                encoder = fn_create_amf();
            }
            if (!encoder) {
                // todo: Can't resize frame now...
                encoder = fn_create_ffmpeg();
            }
            //auto encoder = fn_create_ffmpeg();
            //auto encoder = fn_create_nvenc();
            return encoder;
	    }
	    else {
	        if (name == ECreateEncoderName::kNVENC) {
                return fn_create_nvenc();
	        }
	        else if (name == ECreateEncoderName::kAMF) {
	            return fn_create_amf();
	        }
	        else if (name == ECreateEncoderName::kFFmpeg) {
                return fn_create_ffmpeg();
	        }
	    }
	    return nullptr;
	}

}