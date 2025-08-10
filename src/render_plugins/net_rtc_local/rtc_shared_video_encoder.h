#pragma once

#include <chrono>

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

        static int GetAllVideoEncoderMinBitrate();

        static int GetVideoEncoderCount();

        int32_t InitEncode(const webrtc::VideoCodec* codec_settings, const webrtc::VideoEncoder::Settings& settings) override;

        int32_t Release() override;

        int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override {
            mEncodedImageCallback = callback;
            return WEBRTC_VIDEO_CODEC_OK;
        }

        virtual void SetRates(const RateControlParameters& parameters) override;

        int32_t Encode(const webrtc::VideoFrame& frame, const std::vector<webrtc::VideoFrameType>* frame_types) override;

        webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

        virtual void OnPacketLossRateUpdate(float packet_loss_rate) override;

        // Inform the encoder when the round trip time changes.
        //
        // Input:   - rtt_ms            : The new RTT, in milliseconds.
        virtual void OnRttUpdate(int64_t rtt_ms) override;

        void RegisterOnResetVideoRateCallback(){}

        int GetTargetBitrate() { return mTargetBitrate; }

        int GetMaxBitrate() const;

        void RegisterEvents();

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
        //ObjectCounter<RtcSharedVideoEncoder> object_counter_;
    };

} // namespace dl