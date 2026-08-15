#pragma once

#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <common_video/h264/h264_common.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include "px_common_new/webrtc_helper.h"

namespace px
{
    class RtcServer;
    class RtcLocalPlugin;

    // Implementation of video encoder factory
    class RtcVideoEncoderFactory : public webrtc::VideoEncoderFactory {
    public:
        RtcVideoEncoderFactory(RtcLocalPlugin* plugin, const std::shared_ptr<RtcServer>& server) : supported_formats_(webrtc::SupportedH264Codecs()) {
            this->plugin_ = plugin;
            this->server_ = server;
            supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline, webrtc::H264Level::kLevel3_1, "1"));
            supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline, webrtc::H264Level::kLevel3_1, "0"));
            supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel3_1, "1"));
            supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel3_1, "0"));
            supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileMain, webrtc::H264Level::kLevel3_1, "1"));
            supported_formats_.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileMain, webrtc::H264Level::kLevel3_1, "0"));
        }
        ~RtcVideoEncoderFactory() override = default;

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
        RtcLocalPlugin* plugin_ = nullptr;
        std::shared_ptr<RtcServer> server_ = nullptr;
        std::vector<webrtc::SdpVideoFormat> supported_formats_;

    };

}