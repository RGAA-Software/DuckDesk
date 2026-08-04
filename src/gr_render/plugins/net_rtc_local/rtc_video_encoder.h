#pragma once

#include <chrono>
#include <string>

#include <modules/video_coding/codecs/h264/include/h264.h>
#include <common_video/h264/h264_common.h>

#include "tc_common_new/webrtc_helper.h"

namespace tc
{

    extern bool gAdapterBitrate;

    class RtcServer;
    class RtcLocalPlugin;

    class RtcSharedVideoEncoder : public webrtc::VideoEncoder {
    public:
        RtcSharedVideoEncoder(RtcLocalPlugin* plugin, const std::shared_ptr<RtcServer>& server);
        ~RtcSharedVideoEncoder() override;
        int32_t InitEncode(const webrtc::VideoCodec* codec_settings, const webrtc::VideoEncoder::Settings& settings) override;
        int32_t Release() override;
        int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override {
            mEncodedImageCallback = callback;
            return WEBRTC_VIDEO_CODEC_OK;
        }
        void SetRates(const RateControlParameters& parameters) override;
        int32_t Encode(const webrtc::VideoFrame& frame, const std::vector<webrtc::VideoFrameType>* frame_types) override;
        webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;
        void OnPacketLossRateUpdate(float packet_loss_rate) override;

        // Inform the encoder when the round trip time changes.
        //
        // Input:   - rtt_ms            : The new RTT, in milliseconds.
        void OnRttUpdate(int64_t rtt_ms) override;

        int GetVideoEncoderMinBitrate();
        void RegisterOnResetVideoRateCallback(){}
        int GetTargetBitrate() { return mTargetBitrate; }
        int GetMaxBitrate() const;

    private:
        RtcLocalPlugin* plugin_ = nullptr;
        std::shared_ptr<RtcServer> server_ = nullptr;
        webrtc::EncodedImageCallback* mEncodedImageCallback;
        uint16_t mLastVideoFrameIndex = 0;
        bool mWaitIDRFrame = true;
        std::chrono::high_resolution_clock::time_point mLastInsertIDRTime;
        std::atomic_int mTargetBitrate = { 0 };
        absl::optional<uint16_t> last_encoded_frame_seq_;
        int mWidth = 0;
        int mHeight = 0;
        int encoder_index_ = 0;
        bool insert_timer_sei_ = true;
        uint16_t last_encoded_frame_index_ = 0;
        // 当前绑定的采集显示器名:切屏时重置 IDR 等待与编码序号基线
        std::string last_mon_name_;
    };

} // namespace tc