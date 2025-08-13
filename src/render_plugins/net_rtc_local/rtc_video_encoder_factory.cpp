#include "rtc_video_encoder_factory.h"
#include "rtc_video_encoder.h"
#include "tc_common_new/log.h"

namespace tc
{

    std::unique_ptr<webrtc::VideoEncoder> RtcVideoEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)  {
        return std::make_unique<RtcSharedVideoEncoder>(plugin_, server_);
    }

    webrtc::VideoEncoderFactory::CodecSupport RtcVideoEncoderFactory::QueryCodecSupport(const webrtc::SdpVideoFormat& format,
        absl::optional<std::string> scalability_mode) const {
        webrtc::VideoEncoderFactory::CodecSupport codec_support;
        codec_support.is_supported = true;
        return codec_support;
    }

} // namespace tc
