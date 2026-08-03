#include "rtc_video_encoder.h"
#include <atomic>
#include "tc_common_new/log.h"
#include "tc_common_new/time_util.h"
#include "h264_sei_helper.h"
#include "rtc_local_plugin.h"
#include "video_source_impl.h"
#include "settings/rd_settings.h"
#include "gr_render/plugins/plugin_ids.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"
#include "gr_render/plugin_interface/gr_video_encoder_plugin.h"
#include "gr_render/plugin_interface/gr_frame_carrier_plugin.h"

namespace tc
{

    bool gAdapterBitrate = true;

    RtcSharedVideoEncoder::RtcSharedVideoEncoder(RtcLocalPlugin* plugin, const std::shared_ptr<RtcServer>& server) {
        plugin_ = plugin;
        server_ = server;
    }

    RtcSharedVideoEncoder::~RtcSharedVideoEncoder() {
        LOGI("rtc shared encoder released.");
    }

    int RtcSharedVideoEncoder::GetVideoEncoderMinBitrate()
    {
        return this->GetTargetBitrate();
    }

    int32_t RtcSharedVideoEncoder::InitEncode(const webrtc::VideoCodec* codec_settings, const webrtc::VideoEncoder::Settings& settings)
    {
        LOGI("InitEncode start bitrate {} kbps", codec_settings->startBitrate);
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t RtcSharedVideoEncoder::Release()
    {
        return WEBRTC_VIDEO_CODEC_OK;
    }

    void RtcSharedVideoEncoder::SetRates(const RateControlParameters& parameters)
    {
        std::stringstream ss;
        ss << "rtc update bitrate:" << parameters.bitrate.get_sum_kbps();
        ss << " kbps target bitrat:" << parameters.target_bitrate.get_sum_kbps();
        ss << " kbps bandwidth_allocation:" << parameters.bandwidth_allocation.kbps();
        ss << " kbps fps:" << parameters.framerate_fps;
        LOGI("SetRates: {}", ss.str());
        mTargetBitrate = parameters.bitrate.get_sum_bps();

        auto event = std::make_shared<GrPluginConfigEncoder>();
        event->bps_ = parameters.bitrate.get_sum_bps();
        event->fps_ = parameters.framerate_fps;
        plugin_->CallbackEvent(event);
    }

    int32_t RtcSharedVideoEncoder::Encode(const webrtc::VideoFrame& frame, const std::vector<webrtc::VideoFrameType>* frame_types)
    {
        if (!mEncodedImageCallback) {
            RTC_LOG(LS_WARNING) << "RegisterEncodeCompleteCallback() not called";
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }

        mWidth = frame.width();
        mHeight = frame.height();
        rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer = frame.video_frame_buffer();
        if (buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
            RTC_LOG(LS_WARNING) << "buffer type must be kNative";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        if (frame_types) {
            if (!frame_types->empty()) {
                if (frame_types->at(0) == webrtc::VideoFrameType::kVideoFrameKey) {
                    LOGI("Rtc request to insert an I Frame.");
                    // 请求主编码管线产一个 IDR,本次 Encode 继续等这个关键帧。
                    plugin_->InsertIdr();
                }
            }
        }

        rtc::scoped_refptr<NotifyFrameFrameBuffer> native_buffer = rtc::scoped_refptr<NotifyFrameFrameBuffer>(static_cast<NotifyFrameFrameBuffer*>(buffer.get()));
        if (!native_buffer) {
            RTC_LOG(LS_WARNING) << "Dynamic cast to NotifyFrameFrameBuffer failed";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        // to do encode
        // 注意:这里不再对共享 NVENC/FrameCarrier 插件做重复 Init(旧代码每次
        // Encode 都用假 monitor 名重置主编码器,会搞坏整条编码管线)。
        // 本 encoder 只是"搬运工":等主编码管线产出同 frame_index 的编码帧。

        auto beg_ts = TimeUtil::GetCurrentTimestamp();
        std::shared_ptr<RtcLocalEncodedVideoFrame> encoded_video_frame = nullptr;
        int try_count = 0;
        while (try_count < 50) {
            encoded_video_frame = plugin_->PopEncodedVideoFrame(frame.id());
            if (!encoded_video_frame || !encoded_video_frame->data_) {
                try_count++;
                TimeUtil::DelayBySleep(2);
                continue;
            }
            break;
        }
        if (!encoded_video_frame) {
            // 主编码管线(可能因软编码较慢)还没有产出这一帧——静默丢帧即可,
            // webrtc 会按实际产出节奏发送;不要反复 InsertIdr/返回 TIMEOUT。
            static std::atomic_uint64_t drop_count = 0;
            auto drops = ++drop_count;
            if (drops == 1 || drops % 100 == 0) {
                LOGW("drop frame, encoder pipeline behind, index: {}, total drops: {}", frame.id(), drops);
            }
            return WEBRTC_VIDEO_CODEC_OK;
        }
        plugin_->SetClearOlderFramesBaseline(encoded_video_frame->timestamp_);
        auto end_ts = TimeUtil::GetCurrentTimestamp();
        auto diff_ts = end_ts - beg_ts;
        //LOGI("wait frame used: {}ms, for index: {}", diff_ts, frame.id());

        // 序号连续性以"编码帧自己的序号"为准(精确未命中时会回退取较新的缓存帧,
        // webrtc 侧的 frame.id() 与编码序号不严格一致)。
        auto encoded_idx = encoded_video_frame->frame_index_;

        // 首帧策略:本 peer 尚未成功发出关键帧前,delta 帧一律丢弃——否则
        // 浏览器拿到 delta 解码失败,只能等下一轮 PLI,首帧被拖慢十几秒。
        if (mWaitIDRFrame && !encoded_video_frame->key_) {
            static std::atomic_uint64_t pre_idr_drops = 0;
            if (++pre_idr_drops % 100 == 1) {
                LOGW("waiting first IDR, drop delta frame idx={} (total {})", encoded_idx, pre_idr_drops.load());
            }
            return WEBRTC_VIDEO_CODEC_OK;
        }

        if (last_encoded_frame_index_ == 0) {
            last_encoded_frame_index_ = encoded_idx;
        }
        auto diff_idx = encoded_idx - last_encoded_frame_index_;
        if (diff_idx > 1 && !encoded_video_frame->key_) {
            // 编码序号跳变且不是关键帧:解码端无法接续,请主管线补 IDR,
            // 本帧丢弃(返回 OK 静默),直到拿到关键帧再续。
            LOGI("encoded frame idx gap, current: {}, last: {}, diff: {}, request IDR and drop", encoded_idx, last_encoded_frame_index_, diff_idx);
            plugin_->InsertIdr();
            last_encoded_frame_index_ = encoded_idx;
            return WEBRTC_VIDEO_CODEC_OK;
        }

        last_encoded_frame_index_ = encoded_idx;
        if (encoded_video_frame->key_) {
            mWaitIDRFrame = false;
        }
        // using
        webrtc::EncodedImage encodedImage;
        if (this->insert_timer_sei_) {
            this->insert_timer_sei_ = false;
            SeiInfo sei_info;
            sei_info.frame_index_ = frame.id();// encoded_frame->frame_index;
            sei_info.sender_ts_ = TimeUtil::GetCurrentTimestamp();;
            auto sei = H264SeiHelper::GenCustomSei(sei_info.AsString());
            std::string target_buffer;
            target_buffer.resize(sei.size() + encoded_video_frame->data_->Size());
            memcpy(target_buffer.data(), sei.data(), sei.size());
            memcpy(target_buffer.data() + sei.size(), encoded_video_frame->data_->DataAddr(), encoded_video_frame->data_->Size());
            encodedImage.SetEncodedData(webrtc::EncodedImageBuffer::Create((uint8_t*)target_buffer.data(), target_buffer.size()));
            //RLogI("Insert timer sei,frame id: {}, frame index: {} ts: {}", sei_info.frame_index_, encoded_frame->frame_index, sei_info.sender_ts_);

            //RtcTimePoint tp;
            //tp.frame_idx_ = sei_info.frame_index_;
            //tp.timestamp_ = sei_info.sender_ts_;
            //args->thiz->rtc_server_stat_->AppendSendTimePoint(tp);
        }
        else {
            encodedImage.SetEncodedData(webrtc::EncodedImageBuffer::Create((uint8_t*)encoded_video_frame->data_->DataAddr(), encoded_video_frame->data_->Size()));
        }
        encodedImage.SetRtpTimestamp(frame.timestamp());
        encodedImage.ntp_time_ms_ = frame.ntp_time_ms();
        encodedImage._frameType = encoded_video_frame->key_ ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
        encodedImage._encodedWidth = encoded_video_frame->frame_width_;
        encodedImage._encodedHeight = encoded_video_frame->frame_height_;
        webrtc::CodecSpecificInfo codec_specific;
        codec_specific.codecType = webrtc::VideoCodecType::kVideoCodecH264;
        mEncodedImageCallback->OnEncodedImage(encodedImage, &codec_specific);
        //LOGI("OnEncodedImage callback: {}", frame.id());
        ///// below old

        struct PassArgs
        {
            webrtc::EncodedImageCallback* Callback;
            int64_t NtpTimeMs;
            uint32_t Timestamp;
            RtcSharedVideoEncoder* thiz;
            uint16_t frame_id_;
        } args;

        args.Callback = mEncodedImageCallback;
        args.NtpTimeMs = frame.ntp_time_ms();
        args.Timestamp = frame.timestamp();
        args.thiz = this;
        args.frame_id_ = frame.id();

    //	auto res = GEncodeFrameSignal(insert_idr, frame.id(), [](void* user_data, dlca_webrtc_frame* encoded_frame) {
    //		PassArgs* args = (PassArgs*)user_data;
    //
    //		if (args->thiz->last_encoded_frame_seq_) {
    //			if(encoded_frame->key_frame == 0) {
    //				// 帧序列检查。
    //				// P帧重复
    //				if (args->thiz->last_encoded_frame_seq_ == encoded_frame->frame_index) {
    //					LogW(kLogRtc,"video encoder index {} dup p frame. igonre it seq {}",
    //						args->thiz->encoder_index_,encoded_frame->frame_index);
    //					return;
    //				}
    //				// 不是上一次连续的P帧。
    //				if(uint16_t(*args->thiz->last_encoded_frame_seq_ + 1) != encoded_frame->frame_index) {
    //					LogW(kLogRtc,"video encoder index {} detect encoded seq incontinuity, last encoded seq {} current seq {}",
    //						args->thiz->encoder_index_,*args->thiz->last_encoded_frame_seq_,encoded_frame->frame_index);
    //					GOnNeedInsertIDRDelegate.ExecuteIfBound();
    //					return;
    //				}
    //			}
    //		}
    //		webrtc::EncodedImageCallback* encodedImageCallback = args->Callback;
    //		webrtc::EncodedImage encodedImage;
    //
    //		if (args->thiz->insert_timer_sei_) {
    //			args->thiz->insert_timer_sei_ = false;
    //			SeiInfo sei_info;
    //			sei_info.frame_index_ = args->frame_id_;// encoded_frame->frame_index;
    //			sei_info.sender_ts_ = TimeUtil::GetCurrentTimestamp();;
    //			auto sei = H264SeiHelper::GenCustomSei(sei_info.AsString());
    //			std::string target_buffer;
    //			target_buffer.resize(sei.size() + encoded_frame->size);
    //			memcpy(target_buffer.data(), sei.data(), sei.size());
    //			memcpy(target_buffer.data() + sei.size(), encoded_frame->data, encoded_frame->size);
    //			encodedImage.SetEncodedData(webrtc::EncodedImageBuffer::Create((uint8_t*)target_buffer.data(), target_buffer.size()));
    //			//RLogI("Insert timer sei,frame id: {}, frame index: {} ts: {}", sei_info.frame_index_, encoded_frame->frame_index, sei_info.sender_ts_);
    //
    //			//RtcTimePoint tp;
    //			//tp.frame_idx_ = sei_info.frame_index_;
    //			//tp.timestamp_ = sei_info.sender_ts_;
    //			//args->thiz->rtc_server_stat_->AppendSendTimePoint(tp);
    //		}
    //		else {
    //			encodedImage.SetEncodedData(webrtc::EncodedImageBuffer::Create(encoded_frame->data, encoded_frame->size));
    //		}
    //		encodedImage.SetRtpTimestamp(args->Timestamp);
    //		encodedImage.ntp_time_ms_ = args->NtpTimeMs;
    //		encodedImage._frameType = encoded_frame->key_frame ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
    //		encodedImage._encodedWidth = encoded_frame->width;
    //		encodedImage._encodedHeight = encoded_frame->height;
    //		webrtc::CodecSpecificInfo codec_specific;
    //		codec_specific.codecType = webrtc::VideoCodecType::kVideoCodecH264;
    //		encodedImageCallback->OnEncodedImage(encodedImage, &codec_specific);
    //		args->thiz->last_encoded_frame_seq_ = encoded_frame->frame_index;
    //	}, &args);

        return WEBRTC_VIDEO_CODEC_OK;
    }

    webrtc::VideoEncoder::EncoderInfo RtcSharedVideoEncoder::GetEncoderInfo() const
    {
        webrtc::VideoEncoder::EncoderInfo info;
        info.implementation_name = "NullEncoder";
        info.supports_native_handle = true;
        info.is_hardware_accelerated = true;
        info.has_trusted_rate_controller = false;
        //info.has_internal_source = false;
        info.supports_simulcast = false;
        int min,max;
        if (gAdapterBitrate)
        {
            constexpr auto kDefalutMinBitrate = 1 * 1024 * 1024;
            constexpr auto kDefalutStartBitrate = 15 * 1024 * 1024;
            constexpr auto kDefalutMaxBitrate = 100 * 1024 * 1024;
            min = kDefalutMinBitrate;
            max = kDefalutStartBitrate;
        }
        else
        {
            min = 128 * 1024 * 1024;
            max = 256 * 1024 * 1024;
        }
        int start = max;
        info.resolution_bitrate_limits.push_back(ResolutionBitrateLimits(1 * 1, start, min, max));
        info.resolution_bitrate_limits.push_back(ResolutionBitrateLimits(8192 * 8192, start, min, max));
        return info;
    }

    void RtcSharedVideoEncoder::OnPacketLossRateUpdate(float packet_loss_rate)
    {
        if (packet_loss_rate > 0.01) {
            LOGI("rtc packet loss update: {}", packet_loss_rate);
        }
    }

    // Inform the encoder when the round trip time changes.
    //
    // Input:   - rtt_ms            : The new RTT, in milliseconds.
    void RtcSharedVideoEncoder::OnRttUpdate(int64_t rtt_ms)
    {
        // LOG(kLogLevelInformation) << "rtc rtt update:" << rtt_ms;
    }

    int RtcSharedVideoEncoder::GetMaxBitrate() const
    {
        //return GWebrtcConfig.max_bitrate * (float(mWidth) * mHeight / (1920 * 1080));
        return 0;
    }

} // namespace tc