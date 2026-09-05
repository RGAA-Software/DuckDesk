#include "rtc_video_encoder.h"
#include <atomic>
#include <chrono>
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_common/time_util.h"
#include "h264_sei_helper.h"
#include "webrtc_local_transport.h"
#include "rtc_server.h"
#include "video_source_impl.h"
#include "settings/rd_settings.h"
#include "px_render/modules/module_ids.h"

namespace px {

bool gAdapterBitrate = true;

RtcSharedVideoEncoder::RtcSharedVideoEncoder(const std::shared_ptr<RtcServer>& server) : server_(server) {}

RtcSharedVideoEncoder::~RtcSharedVideoEncoder() {
    LOGI("rtc shared encoder released.");
}

int RtcSharedVideoEncoder::GetVideoEncoderMinBitrate() {
    return this->GetTargetBitrate();
}

int32_t
RtcSharedVideoEncoder::InitEncode(const webrtc::VideoCodec* codec_settings, // NOLINT(gammaray-raw-pointer-boundary): libwebrtc VideoEncoder ABI
                                  const webrtc::VideoEncoder::Settings& settings) {
    LOGI("InitEncode start bitrate {} kbps", codec_settings->startBitrate);
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t RtcSharedVideoEncoder::Release() {
    encoded_image_callback_.reset();
    return WEBRTC_VIDEO_CODEC_OK;
}

void RtcSharedVideoEncoder::SetRates(const RateControlParameters& parameters) {
    mTargetBitrate = parameters.bitrate.get_sum_bps();

    // A wall observer shares the already encoded stream with the single
    // interactive RTC session. Its peer-local BWE may still tune that
    // connection's pacer, but it must never reconfigure the shared
    // physical encoder; otherwise two peers become last-writer-wins on
    // bitrate/fps and can repeatedly reopen or reconfigure the encoder.
    if (server_ && server_->IsWallObserver()) {
        if (!observer_feedback_ignored_.exchange(true)) {
            LOGI("Ignore encoder rate feedback from wall observer.");
        }
        return;
    }

    std::stringstream ss;
    ss << "rtc update bitrate:" << parameters.bitrate.get_sum_kbps();
    ss << " kbps target bitrat:" << parameters.target_bitrate.get_sum_kbps();
    ss << " kbps bandwidth_allocation:" << parameters.bandwidth_allocation.kbps();
    ss << " kbps fps:" << parameters.framerate_fps;
    ss << " mon:" << last_monitor_name_;
    LOGI("SetRates: {}", ss.str());

    // 按屏定向:多 track 时每条 track 的 BWE 只调整自己那块屏的编码器,
    // 否则空 mon_name 会被广播到所有屏,静态屏 track 的低 fps/低码率
    // 会每秒覆盖活跃屏 track 刚写下的配置(两条 BWE 互踩)。
    // last_monitor_name_ 由 Encode 维护,与 SetRates 同在 libwebrtc encoder
    // task queue 上执行,无线程竞争;尚未 Encode 过时为空,退化为广播旧行为。
    server_->QueueEvent(WebRtcConfigureEncoderEvent{
        .monitor_name = last_monitor_name_,
        .bits_per_second = parameters.bitrate.get_sum_bps(),
        .frames_per_second = static_cast<std::uint32_t>(parameters.framerate_fps),
    });
}

int32_t RtcSharedVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) { // NOLINT(gammaray-raw-pointer-boundary): libwebrtc VideoEncoder ABI
    if (!encoded_image_callback_) {
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

    rtc::scoped_refptr<NotifyFrameFrameBuffer> native_buffer =
        rtc::scoped_refptr<NotifyFrameFrameBuffer>(static_cast<NotifyFrameFrameBuffer*>(buffer.get()));
    if (!native_buffer) {
        RTC_LOG(LS_WARNING) << "Dynamic cast to NotifyFrameFrameBuffer failed";
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    if (frame_types) {
        if (!frame_types->empty()) {
            if (frame_types->at(0) == webrtc::VideoFrameType::kVideoFrameKey) {
                LOGI("Rtc request to insert an I Frame, mon: {}", native_buffer->GetMonName());
                ++win_idr_requested_;
                // 请求主编码管线产一个 IDR,本次 Encode 继续等这个关键帧。
                // 按屏定向:多 track 时只给本 track 的屏补 IDR,其它屏不受 PLI 波及。
                server_->RequestEncodedIdr(native_buffer->GetMonName());
            }
        }
    }

    // to do encode
    // 注意:这里不再对共享 NVENC/FrameCarrier 插件做重复 Init(旧代码每次
    // Encode 都用假 monitor 名重置主编码器,会搞坏整条编码管线)。
    // 本 encoder 只是"搬运工":等主编码管线产出同 frame_index 的编码帧。

    // 切屏:采集显示器变化 => 编码流来自另一块屏(新 SPS/PPS + 序号空间),
    // 重新等 IDR 并以新屏当前产出序号引导,只消费之后到达的帧,
    // 否则旧屏残帧/delta 帧会让浏览器解码花屏
    const auto& mon_name = native_buffer->GetMonName();
    const bool monitor_switched = last_monitor_name_ != mon_name;
    if (monitor_switched || native_buffer->IsStreamReset()) {
        if (monitor_switched && !last_monitor_name_.empty()) {
            LOGI("Encoder detected monitor switch: {} -> {}, wait IDR again.", last_monitor_name_, mon_name);
        } else if (native_buffer->IsStreamReset()) {
            LOGI("Encoder detected capture stream reset for {}, wait IDR again.", mon_name);
        }
        last_monitor_name_ = mon_name;
        mWaitIDRFrame = true;
        consumed_seq_ = server_->GetLatestEncodedSeq(mon_name);
        // 新屏 frame_index 序号空间不同,旧时间戳日志作废
        input_ts_log_.clear();
        has_last_sent_ts_ = false;
        // A topology change can restart capture with the same monitor name.
        // Do not forward a delta frame from the prior encoder chain.
        RequestIdrThrottled();
    }

    // 记录当前输入帧时间戳,供编码帧发送时按完整 frame_index 回查
    input_ts_log_.push_back({native_buffer->GetFrameIdx(), frame.timestamp(), frame.ntp_time_ms()});
    while (input_ts_log_.size() > kMaxInputTsLog) {
        input_ts_log_.pop_front();
    }

    // 按编码产出序号顺序取一帧。注意:不做任何忙等——编码器产出速率
    // 低于采集/webrtc 调用速率是常态,没货就静默返回,
    // webrtc 按编码器实际产出节奏发送即可,忙等只会阻塞 webrtc 编码线程打乱 pacing。
    static std::atomic_uint64_t encode_calls = 0;
    if (++encode_calls % 300 == 1) {
        LOGI("Encode call #{}, mon={}, consumed_seq={}", encode_calls.load(), mon_name, consumed_seq_);
    }
    bool seq_gap = false;
    auto encoded_video_frame = server_->ReadNextEncodedVideoFrame(mon_name, consumed_seq_, seq_gap);
    if (!encoded_video_frame || !encoded_video_frame->data_) {
        // 有界等待(≠忙等):Encode 由采集帧驱动,此刻本采集帧的编码通常
        // 即将完成(NVENC 管线延迟 ~10ms)。等它产出立即发出,把"只能搬
        // 上一帧"的固有拾取延迟(实测 age_avg 26ms)压到管线延迟以内。
        // 8ms < 16.6ms 帧周期,给 encoder queue 留有余量;超时按现状
        // 空转返回,不打乱 pacing。新帧到达时插件侧 cv 会立即唤醒。
        if (server_->WaitForEncodedFrame(mon_name, consumed_seq_, 8)) {
            encoded_video_frame = server_->ReadNextEncodedVideoFrame(mon_name, consumed_seq_, seq_gap);
        }
    }
    if (!encoded_video_frame || !encoded_video_frame->data_) {
        return WEBRTC_VIDEO_CODEC_OK;
    }
    consumed_seq_ = encoded_video_frame->seq_;

    // 应急阀:缓存积压超过 ~0.5s(正常应趋近 0)说明生产持续快于消费,
    // 继续发陈旧帧只会让浏览器抖动缓冲把延迟越抬越高(BWE 随之崩盘)。
    // 快进到最新帧;跳过未发 delta 导致断链则补 IDR(节流)等关键帧。
    auto pending = server_->GetCachedFrameCount(mon_name, consumed_seq_);
    bool skipped_unsent = seq_gap;
    if (pending > kMaxBacklogFrames) {
        while (true) {
            bool gap = false;
            auto f = server_->ReadNextEncodedVideoFrame(mon_name, consumed_seq_, gap);
            if (!f || !f->data_) {
                break;
            }
            skipped_unsent = skipped_unsent || gap;
            consumed_seq_ = f->seq_;
            encoded_video_frame = f;
        }
        skipped_unsent = true;
        ++win_backlog_skips_;
        LOGW("encoder backlog > {} frames, skip to newest seq={}", kMaxBacklogFrames, encoded_video_frame->seq_);
    }

    // 首帧策略:本 peer 尚未成功发出关键帧前,delta 帧一律丢弃——否则
    // 浏览器拿到 delta 解码失败,只能等下一轮 PLI,首帧被拖慢十几秒。
    if (mWaitIDRFrame && !encoded_video_frame->key_) {
        ++win_pre_idr_drops_;
        // A monitor switch can skip an IDR that was produced just before
        // this peer reset its per-monitor cursor. Do not rely solely on a
        // later browser PLI: keep requesting a throttled IDR while delta
        // frames prove that the new capture path is alive but undecodable.
        RequestIdrThrottled();
        static std::atomic_uint64_t pre_idr_drops = 0;
        if (++pre_idr_drops % 60 == 1) {
            LOGW("waiting first IDR, drop delta frame seq={} (total {})", encoded_video_frame->seq_, pre_idr_drops.load());
        }
        return WEBRTC_VIDEO_CODEC_OK;
    }

    // 断链(缓存淘汰或应急快进跳过了未发 delta)且本帧不是关键帧:
    // 解码端无法接续,请主管线补 IDR(节流防 IDR 风暴),本帧丢弃,直到拿到关键帧再续。
    if (skipped_unsent && !encoded_video_frame->key_) {
        ++win_chain_broken_;
        LOGW("encoded chain broken at seq {}, request IDR and drop", encoded_video_frame->seq_);
        RequestIdrThrottled();
        mWaitIDRFrame = true;
        return WEBRTC_VIDEO_CODEC_OK;
    }

    if (encoded_video_frame->key_) {
        mWaitIDRFrame = false;
        ++win_key_sent_;
    }
    // using
    webrtc::EncodedImage encodedImage;
    if (this->insert_timer_sei_) {
        this->insert_timer_sei_ = false;
        SeiInfo sei_info;
        sei_info.frame_index_ = frame.id(); // encoded_frame->frame_index;
        sei_info.sender_ts_ = TimeUtil::GetCurrentTimestamp();
        ;
        auto sei = H264SeiHelper::GenCustomSei(sei_info.AsString());
        std::string target_buffer;
        target_buffer.resize(sei.size() + encoded_video_frame->data_->Size());
        memcpy(target_buffer.data(), sei.data(), sei.size());
        memcpy(target_buffer.data() + sei.size(), encoded_video_frame->data_->MutableBytes().data(), encoded_video_frame->data_->Size());
        encodedImage.SetEncodedData(webrtc::EncodedImageBuffer::Create((uint8_t*)target_buffer.data(), target_buffer.size()));
        // RLogI("Insert timer sei,frame id: {}, frame index: {} ts: {}", sei_info.frame_index_, encoded_frame->frame_index, sei_info.sender_ts_);

        // RtcTimePoint tp;
        // tp.frame_idx_ = sei_info.frame_index_;
        // tp.timestamp_ = sei_info.sender_ts_;
        // args->thiz->rtc_server_stat_->AppendSendTimePoint(tp);
    } else {
        encodedImage.SetEncodedData(
            webrtc::EncodedImageBuffer::Create((uint8_t*)encoded_video_frame->data_->MutableBytes().data(), encoded_video_frame->data_->Size()));
    }
    // 用被编码帧的真实采集时间戳(按 frame_index 回查输入日志),而非
    // 当前输入帧的时间戳。详见头文件 input_ts_log_ 注释:盖错时间戳会让
    // 浏览器抖动缓冲目标延迟失控抬升。编码帧滞后输入帧通常仅几十帧以内,
    // 从尾部反向扫几下即可命中。
    uint32_t send_rtp_ts = frame.timestamp();
    int64_t send_ntp_ms = frame.ntp_time_ms();
    const uint64_t want_idx = encoded_video_frame->frame_index_;
    bool ts_matched = false;
    for (auto it = input_ts_log_.rbegin(); it != input_ts_log_.rend(); ++it) {
        if (it->frame_index_ == want_idx) {
            send_rtp_ts = it->rtp_ts_;
            send_ntp_ms = it->ntp_ms_;
            ts_matched = true;
            break;
        }
    }
    if (!ts_matched) {
        ++ts_lookup_miss_;
    }
    // Normalize to NTP epoch (ms since 1900). Accept either already-NTP or Unix.
    constexpr int64_t kNtpJan1970Ms = 2208988800LL * 1000LL;
    const int64_t unix_now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (send_ntp_ms <= 0) {
        send_ntp_ms = unix_now_ms + kNtpJan1970Ms;
    } else if (send_ntp_ms < kNtpJan1970Ms) {
        send_ntp_ms += kNtpJan1970Ms;
    }
    // 发送时间戳必须严格单调。回退时按 90kHz/60 推进,绝不用 +1——
    // +1 会让媒体时钟几乎停滞,Chrome 把到达的帧全部当成"太早"囤到 ~1s。
    if (has_last_sent_ts_) {
        if ((int32_t)(send_rtp_ts - last_sent_rtp_ts_) <= 0) {
            send_rtp_ts = last_sent_rtp_ts_ + kRtpTicksPerFrame;
        }
        if (send_ntp_ms <= last_sent_ntp_ms_) {
            send_ntp_ms = last_sent_ntp_ms_ + (1000 / 60);
        }
    }
    last_sent_rtp_ts_ = send_rtp_ts;
    last_sent_ntp_ms_ = send_ntp_ms;
    has_last_sent_ts_ = true;
    // 帧龄:发送时刻(unix) - 编码完成时记录的 wall clock(unix)
    auto frame_age_ms = unix_now_ms - encoded_video_frame->timestamp_;
    send_age_sum_ += frame_age_ms;
    if (frame_age_ms > send_age_max_)
        send_age_max_ = frame_age_ms;
    encodedImage.SetRtpTimestamp(send_rtp_ts);
    encodedImage.ntp_time_ms_ = send_ntp_ms;
    // RTC factory 只协商 H264。全彩模式主管线会出 HEVC,绝不能再当 H264 塞给浏览器。
    if (encoded_video_frame->video_type_ == static_cast<int>(WebRtcEncodedVideoType::kH265)) {
        static std::atomic_uint64_t hevc_drops = 0;
        if (++hevc_drops % 120 == 1) {
            LOGW("RTC drops HEVC frame seq={} (factory is H264-only); disable full-color for WebRTC", encoded_video_frame->seq_);
        }
        return WEBRTC_VIDEO_CODEC_OK;
    }
    encodedImage._frameType = encoded_video_frame->key_ ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
    encodedImage._encodedWidth = encoded_video_frame->frame_width_;
    encodedImage._encodedHeight = encoded_video_frame->frame_height_;
    webrtc::CodecSpecificInfo codec_specific;
    codec_specific.codecType = webrtc::VideoCodecType::kVideoCodecH264;
    auto cb_result = encoded_image_callback_->get().OnEncodedImage(encodedImage, &codec_specific);
    static std::atomic_uint64_t sent_frames = 0;
    if (++sent_frames % 300 == 1 || cb_result.error != webrtc::EncodedImageCallback::Result::OK) {
        LOGI("sent encoded frame #{}, seq={}, key={}, size={}, cb_err={}, ts_miss={}, ts_log={}, age_avg={}ms, age_max={}ms, cache={}, "
             "win: keys={}, broken={}, idr_req={}, pre_idr_drops={}, backlog_skips={}",
             sent_frames.load(), encoded_video_frame->seq_, encoded_video_frame->key_, encoded_video_frame->data_->Size(), (int)cb_result.error,
             ts_lookup_miss_, input_ts_log_.size(), send_age_count_ > 0 ? send_age_sum_ / send_age_count_ : 0, send_age_max_,
             server_->GetCachedFrameCount(mon_name, consumed_seq_), win_key_sent_, win_chain_broken_, win_idr_requested_, win_pre_idr_drops_,
             win_backlog_skips_);
        send_age_sum_ = 0;
        send_age_max_ = 0;
        send_age_count_ = 0;
        win_key_sent_ = 0;
        win_chain_broken_ = 0;
        win_idr_requested_ = 0;
        win_pre_idr_drops_ = 0;
        win_backlog_skips_ = 0;
    }
    ++send_age_count_;
    ///// below old

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
    //			//RLogI("Insert timer sei,frame id: {}, frame index: {} ts: {}", sei_info.frame_index_, encoded_frame->frame_index,
    // sei_info.sender_ts_);
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

webrtc::VideoEncoder::EncoderInfo RtcSharedVideoEncoder::GetEncoderInfo() const {
    webrtc::VideoEncoder::EncoderInfo info;
    info.implementation_name = "NullEncoder";
    info.supports_native_handle = true;
    info.is_hardware_accelerated = true;
    // trusted=true:关闭 webrtc 输入侧帧丢弃器,Encode 严格按采集速率调用。
    // 之前 false 时,webrtc 的适配器会把生产/消费双双压到一个振荡的"目标 fps"
    // (实测 23~51 秒级抖动,与码率无关)——生产与消费是两个独立实现的跳帧器,
    // 相位漂移导致缓存交替打空/积压;打空时 Encode 空转返回,webrtc 把"帧送进
    // 编码器却再没回来"记为编码器过载,进一步下调目标 fps,正反馈收敛在 ~30fps。
    // 而且发送速率(35fps)长期低于 RTP 时间戳推进速率(60fps),浏览器抖动缓冲
    // 判定每帧都越来越晚,目标延迟线性失控(实测 200s+)。
    // BWE 死锁问题已由 RtcServer 的 SetBitrate 钉死(12M)兜底,pacing 不再
    // 依赖本 flag 的 rate_settings 种子。生产=消费=采集速率,缓存始终有货。
    info.has_trusted_rate_controller = true;
    // info.has_internal_source = false;
    info.supports_simulcast = false;
    int min, max;
    if (gAdapterBitrate) {
        constexpr auto kDefalutMinBitrate = 1 * 1024 * 1024;
        constexpr auto kDefalutStartBitrate = 15 * 1024 * 1024;
        constexpr auto kDefalutMaxBitrate = 100 * 1024 * 1024;
        min = kDefalutMinBitrate;
        max = kDefalutStartBitrate;
    } else {
        min = 128 * 1024 * 1024;
        max = 256 * 1024 * 1024;
    }
    int start = max;
    info.resolution_bitrate_limits.push_back(ResolutionBitrateLimits(1 * 1, start, min, max));
    info.resolution_bitrate_limits.push_back(ResolutionBitrateLimits(8192 * 8192, start, min, max));
    return info;
}

void RtcSharedVideoEncoder::RequestIdrThrottled() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastInsertIDRTime).count();
    if (ms >= 800) {
        mLastInsertIDRTime = now;
        ++win_idr_requested_;
        // 按屏定向:断链重建只给本 track 的屏补 IDR
        server_->RequestEncodedIdr(last_monitor_name_);
    }
}

void RtcSharedVideoEncoder::OnPacketLossRateUpdate(float packet_loss_rate) {
    if (packet_loss_rate > 0.01) {
        LOGI("rtc packet loss update: {}", packet_loss_rate);
    }
}

// Inform the encoder when the round trip time changes.
//
// Input:   - rtt_ms            : The new RTT, in milliseconds.
void RtcSharedVideoEncoder::OnRttUpdate(int64_t rtt_ms) {
    // LOG(kLogLevelInformation) << "rtc rtt update:" << rtt_ms;
}

int RtcSharedVideoEncoder::GetMaxBitrate() const {
    // return GWebrtcConfig.max_bitrate * (float(mWidth) * mHeight / (1920 * 1080));
    return 0;
}

} // namespace px
