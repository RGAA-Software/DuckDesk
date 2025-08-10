#pragma once

#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <common_video/h264/h264_common.h>
#include <modules/video_coding/codecs/h264/include/h264.h>

#include "tc_common_new/webrtc_helper.h"

namespace tc
{

class RtcContext;

// Implementation of video encoder factory
class RtcSharedVideoEncoderFactory : public webrtc::VideoEncoderFactory {
public:
	RtcSharedVideoEncoderFactory(const std::shared_ptr<RtcContext>& ctx) : supported_formats_(webrtc::SupportedH264Codecs()) {
		rtc_ctx_ = ctx;
		supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline, webrtc::H264Level::kLevel3_1, "1"));
		supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline, webrtc::H264Level::kLevel3_1, "0"));
		supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel3_1, "1"));
		supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel3_1, "0"));
		supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileMain, webrtc::H264Level::kLevel3_1, "1"));
		supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileMain, webrtc::H264Level::kLevel3_1, "0"));
	}
	virtual ~RtcSharedVideoEncoderFactory() override {}

	//virtual webrtc::VideoEncoderFactory::CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override
	//{
	//	webrtc::VideoEncoderFactory::CodecInfo codecInfo;
	//	codecInfo.has_internal_source = false;
	//	return codecInfo;
	//}

	CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& format, absl::optional<std::string> scalability_mode) const override;
	std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format) override;

	std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
		return supported_formats_;
	}

private:
	std::vector<webrtc::SdpVideoFormat> supported_formats_;
	std::shared_ptr<RtcContext> rtc_ctx_ = nullptr;
};

}