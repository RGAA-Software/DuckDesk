#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <string>
#include <utility>

#include <modules/video_coding/codecs/h264/include/h264.h>
#include <common_video/h264/h264_common.h>

#include "px_common_new/webrtc_helper.h"

namespace px
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
        // 请求主管线补 IDR,800ms 节流,防断链恢复时刷 IDR 风暴
        void RequestIdrThrottled();

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
        // 编码产出序号消费游标:严格按编码器产出顺序取帧(见 RtcLocalPlugin 注释),
        // 切屏/首连时以该屏当前最大序号引导,只消费之后到达的帧
        uint64_t consumed_seq_ = 0;
        // 缓存积压应急阀值(帧):超过则快进到最新帧,避免发送陈旧画面
        static constexpr size_t kMaxBacklogFrames = 2;
        // 当前绑定的采集显示器名:切屏时重置 IDR 等待与消费游标
        std::string last_mon_name_;

        // 输入帧时间戳日志:按完整 frame_index 回查采集 RTP/NTP(勿用低 16 位 id)。
        struct InputTsEntry {
            uint64_t frame_index_;
            uint32_t rtp_ts_;
            int64_t ntp_ms_;
        };
        std::deque<InputTsEntry> input_ts_log_;
        static constexpr size_t kMaxInputTsLog = 600;
        uint64_t ts_lookup_miss_ = 0;
        // 发送时间戳单调性保护
        uint32_t last_sent_rtp_ts_ = 0;
        int64_t last_sent_ntp_ms_ = 0;
        bool has_last_sent_ts_ = false;
        static constexpr uint32_t kRtpTicksPerFrame = 90000 / 60;
        // 帧龄诊断(发送时刻-采集时刻),300 帧窗口输出后清零
        int64_t send_age_sum_ = 0;
        int64_t send_age_max_ = 0;
        int64_t send_age_count_ = 0;
        // 流性状诊断(300 帧窗口累计,随 sent encoded frame 日志输出后清零):
        // 关键帧占比/断链重建/IDR 请求频率是"fps 数字不低但卡"的直接证据
        // (Chrome 遇断链要等 IDR 重同步,IDR 风暴则解码尖峰+缓冲抬升)
        uint64_t win_key_sent_ = 0;       // 窗口内发出的关键帧数
        uint64_t win_chain_broken_ = 0;   // 窗口内 delta 链断裂丢弃次数
        uint64_t win_idr_requested_ = 0;  // 窗口内向主管线请求 IDR 次数(含 webrtc frame_types)
        uint64_t win_pre_idr_drops_ = 0;  // 窗口内等首 IDR 丢弃的 delta 帧数
        uint64_t win_backlog_skips_ = 0;  // 窗口内缓存积压快进次数
    };

} // namespace px