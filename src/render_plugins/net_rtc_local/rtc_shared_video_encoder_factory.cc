#include "rtc_shared_video_encoder_factory.h"

#include "rtc_shared_video_encoder.h"
#include "tc_common_new/log.h"

namespace tc
{

std::unique_ptr<webrtc::VideoEncoder> RtcSharedVideoEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)  {
	return std::make_unique<RtcSharedVideoEncoder>(nullptr);
}

webrtc::VideoEncoderFactory::CodecSupport RtcSharedVideoEncoderFactory::QueryCodecSupport(const webrtc::SdpVideoFormat& format,
    absl::optional<std::string> scalability_mode) const {
    webrtc::VideoEncoderFactory::CodecSupport codec_support;
    codec_support.is_supported = true;
    LOGI("QueryCodecSupport");
    return codec_support;
}

} // namespace dl
