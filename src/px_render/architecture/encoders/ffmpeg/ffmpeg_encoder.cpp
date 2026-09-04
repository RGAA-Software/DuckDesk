//
// Created by RGAA on 18/02/2025.
//

#include "ffmpeg_encoder.h"
#include <algorithm>
#include <atomic>
#include <libyuv.h>
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "px_common_new/file.h"
#include "px_common_new/time_util.h"
#include "px_common_new/defer.h"
#include "px_common_new/string_util.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "ffmpeg_encoder_plugin.h"

namespace px
{

    FFmpegEncoder::FFmpegEncoder(FFmpegEncoderPlugin* plugin) {
        plugin_ = plugin;
        fps_stat_ = std::make_shared<FpsStat>();
    }

    bool FFmpegEncoder::Init(const EncoderConfig& config, const std::string& monitor_name) {
        InitLog();
        encoder_config_ = config;
        // QSV 不支持 yuv444,全彩模式下让选择链回退到软编(x264 支持 444)
        if (EHardwareEncoder::kQsv == encoder_config_.Hardware && encoder_config_.enable_full_color_mode_) {
            LOGW("QSV does not support yuv444, skip it under full color mode.");
            return false;
        }
        const char* codec_name = nullptr;
        if (EVideoCodecType::kHEVC == config.codec_type) {
            if (EHardwareEncoder::kNvEnc == encoder_config_.Hardware && plugin_->IsHardwareEnabled()) {
                codec_name = "hevc_nvenc";
                display_encoder_name_ = "F_NVENC";
            }
            // FFmpeg's implementation is so bad.
            // else if (EHardwareEncoder::kAmf == encoder_config_.Hardware && plugin_->IsHardwareEnabled()) {
            //     codec_name = "hevc_amf";
            //     display_encoder_name_ = "F_AMF";
            // }
            else if (EHardwareEncoder::kQsv == encoder_config_.Hardware && plugin_->IsHardwareEnabled()) {
                codec_name = "hevc_qsv";
                display_encoder_name_ = "F_QSV";
            }
            else {
                codec_name = "libx265";
                display_encoder_name_ = "S/W";
            }
        }
        else if (EVideoCodecType::kH264 == config.codec_type) {
            if (EHardwareEncoder::kNvEnc == encoder_config_.Hardware && plugin_->IsHardwareEnabled()) {
                codec_name = "h264_nvenc";
                display_encoder_name_ = "F_NVENC";
            }
            // FFmpeg's implementation is so bad.
            // else if (EHardwareEncoder::kAmf == encoder_config_.Hardware && plugin_->IsHardwareEnabled()) {
            //     codec_name = "h264_amf";
            //     display_encoder_name_ = "F_AMF";
            // }
            else if (EHardwareEncoder::kQsv == encoder_config_.Hardware && plugin_->IsHardwareEnabled()) {
                // Intel Quick Sync:无 N/A 卡机器上的硬编兜底。
                // 实测 loopback 场景 x264 软编 1080p 长期占用一个大核以上,
                // 与采集线程/同机浏览器抢 CPU,把 DDA 采集压到 32~40fps,
                // 这是 web client 帧率不足的最终瓶颈。
                codec_name = "h264_qsv";
                display_encoder_name_ = "F_QSV";
            }
            else {
                codec_name = "libx264";
                display_encoder_name_ = "S/W";
            }
        }
        codec_name_ = codec_name;
        if (encoder_config_.fps <= 0) {
            encoder_config_.fps = 60;
        }
        if (encoder_config_.bitrate < 1000000) {
            encoder_config_.bitrate = 1000000;
        }

        if (!OpenCodec((uint32_t)encoder_config_.bitrate)) {
            return false;
        }

        frame_ = av_frame_alloc();
        frame_->width = codec_ctx_->width;
        frame_->height = codec_ctx_->height;
        frame_->format = codec_ctx_->pix_fmt;

        av_frame_get_buffer(frame_, 0);
        packet_ = av_packet_alloc();
        LOGI("Line 1: {} 2: {} 3: {}", frame_->linesize[0], frame_->linesize[1], frame_->linesize[2]);
        return true;
    }

    bool FFmpegEncoder::OpenCodec(uint32_t bitrate) {
        auto encoder_id = encoder_config_.codec_type == EVideoCodecType::kHEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
        const AVCodec* encoder = avcodec_find_encoder_by_name(codec_name_.c_str());
        if (nullptr == encoder) {
            LOGE("Could not find encoder for:{}", codec_name_);
            return false;
        }
        if (codec_ctx_) {
            avcodec_free_context(&codec_ctx_);
        }
        codec_ctx_ = avcodec_alloc_context3(encoder);
        if (!codec_ctx_) {
            LOGE("avcodec_alloc_context3 error!");
            return false;
        }
        if (bitrate < 1000000) {
            bitrate = 1000000;
        }

        codec_ctx_->width = encoder_config_.encode_width;
        codec_ctx_->height = encoder_config_.encode_height;
        codec_ctx_->time_base = { 1, encoder_config_.fps };
        codec_ctx_->framerate = { encoder_config_.fps, 1 };
        codec_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        if (encoder_config_.enable_full_color_mode_) {
            codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P;
        }
        if (EHardwareEncoder::kQsv == encoder_config_.Hardware) {
            // QSV 的系统内存输入只认 NV12(不认平面 YUV420P),拷贝时做 I420->NV12
            codec_ctx_->pix_fmt = AV_PIX_FMT_NV12;
            // QSV 需要硬件设备上下文(oneVPL dispatcher 运行时再找 Intel 驱动里的
            // Media 运行时;找不到说明机器没有可用的 Intel 硬编,回退软编)
            if (!hw_device_ctx_) {
                auto hw_err = av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_QSV, nullptr, nullptr, 0);
                if (hw_err < 0) {
                    LOGE("create QSV hw device failed, err: {} (no Intel Media runtime on this machine)", hw_err);
                    return false;
                }
                LOGI("QSV hw device created.");
            }
            codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
        }
        codec_ctx_->thread_count = std::min(16, (int)std::thread::hardware_concurrency());
        codec_ctx_->thread_type = FF_THREAD_SLICE;
        codec_ctx_->gop_size = gop_size_;
        codec_ctx_->max_b_frames = 0;
        codec_ctx_->bit_rate = bitrate;
        // VBV(HRD)约束:码率真正生效。此前同时设了 crf=23,x264 实际跑 CRF 模式,
        // bit_rate 完全被忽略:IDR 极大、delta 极小,码率曲线剧烈抖动,
        // webrtc 的码率统计/帧率控制被反复打崩(连接几秒内 fps 38->2)。
        codec_ctx_->rc_max_rate = bitrate;
        codec_ctx_->rc_buffer_size = (int)(bitrate / 2);

        LOGI("ffmpeg encoder (re)open: {}, bitrate: {}, fps: {}, {}x{}, threads: {}, gop: {}",
             codec_name_, codec_ctx_->bit_rate, encoder_config_.fps,
             encoder_config_.encode_width, encoder_config_.encode_height,
             codec_ctx_->thread_count, codec_ctx_->gop_size);

        AVDictionary* param = nullptr;
        if (EHardwareEncoder::kNvEnc == encoder_config_.Hardware) {
            av_dict_set(&param, "preset", "llhp", 0);
            av_dict_set(&param, "tune", "ull", 0);
            av_dict_set(&param, "zerolatency", "1", 0);
        }
        else if (EHardwareEncoder::kQsv == encoder_config_.Hardware) {
            // QSV:async_depth=1 关掉编码器内部多帧流水(默认 4,徒增 4 帧延迟);
            // 码控由上面的 bit_rate/rc_max_rate/rc_buffer_size 驱动(CBR)。
            av_dict_set(&param, "preset", "veryfast", 0);
            av_dict_set(&param, "async_depth", "1", 0);
        }
        //else if (EHardwareEncoder::kAmf == encoder_config_.Hardware) {
        //    av_dict_set(&param, "quality", "speed", 0);
        //    av_dict_set(&param, "bf_ref", "0", 0);
        //    av_dict_set(&param, "header_insertion_mode", "idr", 0);
        //    av_dict_set(&param, "rc", "cqp", 0);
        //    av_dict_set(&param, "profile", "main", 0);
        //    // av_dict_set(&param, "usage", "ultralowlatency", 0);
        //}
        else {
            av_dict_set(&param, "preset", "ultrafast", 0);
            av_dict_set(&param, "tune", "zerolatency", 0);
        }

        if (encoder_id == AV_CODEC_ID_H264
            && ShouldEnableForcedH264Idr(codec_name_)) {
            // 不能再设 crf:会与 VBV 码控冲突导致码率失控(见上方 VBV 注释)。
            // libx264 与 h264_nvenc 均支持 forced-idr；没有它时 NVENC 会把
            // AV_PICTURE_TYPE_I 请求编码为非 IDR I 帧，新接收端仍无法解码。
            av_dict_set(&param, "forced-idr", "1", 0);
        }
        if (encoder_id == AV_CODEC_ID_H265) {
            av_dict_set(&param, "x265-params", "qp=20", 0);
            av_dict_set(&param, "tune", "zero-latency", 0);
        }

        auto ret = avcodec_open2(codec_ctx_, encoder, &param);
        av_dict_free(&param);
        if (ret != 0) {
            LOGE("avcodec_open2 error : {}", ret);
            return false;
        }
        // 重开=全新流:首帧必须 IDR,对端 delta 链无感续接
        insert_idr_ = true;
        return true;
    }

    static void CopyToFrame(AVFrame* frame, uint8_t* src_y, uint8_t* src_u, uint8_t* src_v) {
        int y_h = frame->height;
        int uv_h = frame->height / 2;

        int y_w = frame->width;
        int uv_w = frame->width / 2;

        // Y plane
        for (int i = 0; i < y_h; i++) {
            memcpy(frame->data[0] + i * frame->linesize[0],
                   src_y + i * y_w,
                   y_w);
        }

        // U plane
        for (int i = 0; i < uv_h; i++) {
            memcpy(frame->data[1] + i * frame->linesize[1],
                   src_u + i * uv_w,
                   uv_w);
        }

        // V plane
        for (int i = 0; i < uv_h; i++) {
            memcpy(frame->data[2] + i * frame->linesize[2],
                   src_v + i * uv_w,
                   uv_w);
        }
    }

    bool FFmpegEncoder::Encode(std::shared_ptr<Image> image,
                               uint64_t frame_index,
                               const CaptureVideoFrame& capture_frame) {
        auto beg = TimeUtil::GetCurrentTimestamp();

        // WebRTC BWE 随动 1:目标码率变化 -> 节流(>=3s 且幅度>15%)重开 x264。
        // 否则编码器永远按初始码率产出,BWE 下降时 pacing 队列只进不出,
        // 延迟螺旋把整个流拖死。
        // 抗震荡:GCC 在客户端高负载下会给出 1Mbps 量级的噪声目标(实测同一时刻
        // 到达侧仍能跑 6Mbps+,说明低估不对应真实链路能力)。跟随它只会让 x264
        // 每 3s 重开一次。加 LAN 地板 4M;下调需持续 8s 才跟随,上调维持原节流。
        auto target_bps = target_bitrate_.load();
        if (target_bps > 0 && codec_ctx_ && codec_ctx_->bit_rate > 0) {
            constexpr int64_t kLanBitrateFloor = 4 * 1000 * 1000;
            target_bps = (uint32_t)std::max<int64_t>((int64_t)target_bps, kLanBitrateFloor);
            auto cur_bps = codec_ctx_->bit_rate;
            auto diff = (int64_t)target_bps > cur_bps ? (int64_t)target_bps - cur_bps : cur_bps - (int64_t)target_bps;
            if ((int64_t)target_bps < cur_bps * 85 / 100) {
                if (low_target_since_ == 0) low_target_since_ = beg;
            } else {
                low_target_since_ = 0;
            }
            bool down_ok = low_target_since_ > 0 && beg - low_target_since_ >= 8000;
            bool up_ok = (int64_t)target_bps > cur_bps;
            if (diff * 100 / cur_bps > 15 && beg - last_reopen_ts_ >= 3000 && (up_ok || down_ok)) {
                LOGI("BWE follow: reopen encoder, bitrate {} -> {}", cur_bps, target_bps);
                last_reopen_ts_ = beg;
                low_target_since_ = 0;
                OpenCodec(target_bps);
            }
        }

        // WebRTC BWE 随动 2:目标 fps 低于采集 fps -> 输入侧按时间间隔跳帧。
        // 生产速率必须忠实跟随 webrtc 的消费速率(SetRates fps),否则缓存积压。
        // 爬坡期 fps 过低的问题由 RtcServer 的 SetBitrate 起始种子兜底。
        // 跳帧不产生编码事件,delta 链(seq 连续性)在事件侧编号,不受影响。
        // 注意:曾经的"按 SetRates fps 跳帧"已废弃——webrtc 侧已改为
        // has_trusted_rate_controller=true(关闭其输入丢帧器),消费速率恒等于
        // 采集速率;生产若再跳帧,消费>生产会导致缓存打空、Encode 空转,反而
        // 被 webrtc 记为编码器过载。带宽控制只由上面的码率跟随承担。
        last_encode_ts_ = beg;

        auto img_width = image->width;
        auto img_height = image->height;
        auto image_data = image->data;

        //LOGI("Encode frame: {}, frame index: {}", cap_video_frame.display_name_, frame_index);

        // re-create when width/height changed
        // todo
        frame_->pts = (int64_t)frame_index;
        if (insert_idr_ || capture_frame.request_idr_) {
            insert_idr_ = false;
            frame_->flags |= AV_FRAME_FLAG_KEY;
            frame_->pict_type = AV_PICTURE_TYPE_I;
            LOGI("Insert an I Frame!");
        } else {
            frame_->flags &= ~AV_FRAME_FLAG_KEY;
            frame_->pict_type = AV_PICTURE_TYPE_NONE;
        }
        int y_size = img_width * img_height;
        int uv_size = img_width * img_height / 4;
        if (RawImageType::kI420 == image->raw_img_type_) {
            uv_size = img_width * img_height / 4;
        }
        else if (RawImageType::kI444 == image->raw_img_type_) {
            //LOGI("RawImageType::kI444");
            uv_size = img_width * img_height;
        }
        //memcpy(frame_->data[0], image_data->CStr(), y_size);
        //memcpy(frame_->data[1], image_data->CStr() + y_size, uv_size);
        //memcpy(frame_->data[2], image_data->CStr() + y_size + uv_size, uv_size);

        auto y = image_data->CStr();
        auto u = image_data->CStr() + y_size;
        auto v = image_data->CStr() + y_size + uv_size;
        if (AV_PIX_FMT_NV12 == codec_ctx_->pix_fmt) {
            // QSV 输入:I420 -> NV12(Y 平面 + 交错 UV)
            if (RawImageType::kI420 != image->raw_img_type_) {
                LOGE("QSV(NV12) encoder only accepts I420 input!");
                return false;
            }
            libyuv::I420ToNV12((const uint8_t*)y, img_width,
                               (const uint8_t*)u, img_width / 2,
                               (const uint8_t*)v, img_width / 2,
                               frame_->data[0], frame_->linesize[0],
                               frame_->data[1], frame_->linesize[1],
                               img_width, img_height);
        } else {
            CopyToFrame(frame_, (uint8_t*)y, (uint8_t*)u, (uint8_t*)v);
        }

        int send_result = avcodec_send_frame(codec_ctx_, frame_);
        if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
            plugin_->DisableHardware();
            LOGE("encode failed, err: {}, <! hardware disabled !>", send_result);
            return false;
        }
        //LOGI("avcodec_send_frame send_result: {}", send_result);

        while (true) {
            int receive_result = avcodec_receive_packet(codec_ctx_, packet_);
            //LOGI("avcodec_receive_packet receiveResult: {}", receiveResult);
            if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
                break;
            }

            bool key_frame = (packet_->flags & AV_PKT_FLAG_KEY);
            auto encoded_data = Data::Make((char*)packet_->data, packet_->size);

            auto event = std::make_shared<PxPluginEncodedVideoFrameEvent>();
            event->type_ = [=, this]() {
                if (encoder_config_.codec_type == EVideoCodecType::kHEVC) {
                    return PxPluginEncodedVideoType::kH265;
                } else if (encoder_config_.codec_type == EVideoCodecType::kH264) {
                    return PxPluginEncodedVideoType::kH264;
                } else {
                    return PxPluginEncodedVideoType::kH264;
                }
                }();
            event->data_ = encoded_data;
            event->frame_width_ = img_width;
            event->frame_height_ = img_height;
            event->key_frame_ = key_frame;
            event->frame_index_ = frame_index;
            event->capture_frame_ = capture_frame;
            if (AV_PIX_FMT_YUV420P == codec_ctx_->pix_fmt) {
                event->frame_format_ = RawImageType::kI420;
            }
            else if (AV_PIX_FMT_YUV444P == codec_ctx_->pix_fmt) {
                event->frame_format_ = RawImageType::kI444;
            }
            plugin_->CallbackEvent(event);

            auto end = TimeUtil::GetCurrentTimestamp();
            auto diff = end - beg;
            if (encode_durations_.size() >= 180) {
                encode_durations_.pop_front();
            }
            encode_durations_.push_back((int32_t)diff);

            // 生产侧诊断:每 300 帧输出一次 x264 平均耗时与实际产出 fps,
            // 用于区分"x264 本身慢"与"enc 线程串行/被跳帧"
            {
                static std::atomic_uint64_t prod_frames = 0;
                static int64_t prod_window_beg = 0;
                auto cnt = ++prod_frames;
                if (prod_window_beg == 0) prod_window_beg = end;
                if (cnt % 300 == 0) {
                    int64_t sum = 0;
                    for (auto d : encode_durations_) sum += d;
                    auto avg = encode_durations_.empty() ? 0 : sum / (int64_t)encode_durations_.size();
                    auto wall = end - prod_window_beg;
                    prod_window_beg = end;
                    LOGI("prod stat: frames={}, wall={}ms, out_fps={:.1f}, x264_avg={}ms, x264_max={}ms",
                         cnt, wall, wall > 0 ? 300000.0 / wall : 0.0, avg,
                         encode_durations_.empty() ? 0 : *std::max_element(encode_durations_.begin(), encode_durations_.end()));
                }
            }

            fps_stat_->Tick();

            av_packet_unref(packet_);
        }
        return true;
    }

    void FFmpegEncoder::InsertIdr() {
        insert_idr_ = true;
    }

    void FFmpegEncoder::Exit() {
        av_packet_unref(packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&codec_ctx_);
        if (hw_device_ctx_) {
            av_buffer_unref(&hw_device_ctx_);
        }
    }

    int32_t FFmpegEncoder::GetEncodeFps() {
        return fps_stat_->value();
    }

    std::vector<int32_t> FFmpegEncoder::GetEncodeDurations() {
        std::vector<int32_t> result;
        for (const auto& item : encode_durations_) {
            result.push_back(item);
        }
        return result;
    }

    void FFmpegEncoder::InitLog() {
        std::call_once(init_log_flag_, []() {
            av_log_set_level(AV_LOG_WARNING);
            av_log_set_callback([](void* ptr, int level, const char* fmt, va_list vl)
                {
                    static int print_prefix = 1;
                    std::string line;
                    line.resize(4096);
                    av_log_format_line(ptr, level, fmt, vl, line.data(), line.size(), &print_prefix);
                    line = StringUtil::Trim(line);
                    if (level <= AV_LOG_WARNING)
                        LOGI("ffmpeg_wlog:{}", line.c_str());
                }
            );
        });
    }

    std::string FFmpegEncoder::GetDisplayEncoderName() {
        return display_encoder_name_;
    }

    EncoderConfig FFmpegEncoder::GetEncoderConfig() {
        return encoder_config_;
    }
}
