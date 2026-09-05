//
// Created by RGAA on 18/02/2025.
//

#ifndef PX_FFMPEG_ENCODER_H
#define PX_FFMPEG_ENCODER_H

#include <memory>
#include <mutex>
#include "ffmpeg_encoder_defs.h"
#include "px_encoder/encoder_config.h"
#include "px_common/fps_stat.h"
#include "px_capture/capture_message.h"

extern "C" {
    #include "libavcodec/avcodec.h"
    #include <libavutil/opt.h>
    #include <libavutil/hwcontext.h>
}

namespace px
{

    class Data;
    class Image;
    class FfmpegVideoEncoder;

    class FFmpegEncoder {
    public:
        explicit FFmpegEncoder(
            const std::shared_ptr<FfmpegVideoEncoder>& owner);
        bool Init(const EncoderConfig& config, const std::string& monitor_name);
        bool Encode(std::shared_ptr<Image> image,
                    uint64_t frame_index,
                    const CaptureVideoFrame& capture_frame);
        // WebRTC BWE 随动:目标码率(下一次 Encode 时节流重开 x264)与目标帧率(输入侧跳帧)
        void SetTargetBitrate(uint32_t bps) { target_bitrate_ = bps; }
        void SetTargetFps(uint32_t fps) { target_fps_ = fps; }
        void InsertIdr();
        void Exit();
        int32_t GetEncodeFps();
        std::vector<int32_t> GetEncodeDurations();
        std::string GetDisplayEncoderName();
        EncoderConfig GetEncoderConfig();
    private:
        std::weak_ptr<FfmpegVideoEncoder> owner_;
        AVCodecContext* codec_ctx_ = nullptr;
        AVFrame* frame_ = nullptr;
        AVPacket* packet_ = nullptr;
        // QSV 硬件设备上下文(oneVPL),仅 EHardwareEncoder::kQsv 时创建,重开复用
        AVBufferRef* hw_device_ctx_ = nullptr;
        int gop_size_ = 60;
        bool insert_idr_ = false;
        EncoderConfig encoder_config_;
        std::shared_ptr<FpsStat> fps_stat_ = nullptr;
        std::deque<int32_t> encode_durations_;
        std::once_flag init_log_flag_;
        std::string display_encoder_name_;
        // BWE 随动状态
        std::atomic_uint32_t target_bitrate_{0};
        std::atomic_uint32_t target_fps_{0};
        int64_t last_reopen_ts_ = 0;
        int64_t last_encode_ts_ = 0;
        // BWE 抗震荡:低码率目标的首次出现时刻,持续 8s 才真正下调跟随
        int64_t low_target_since_ = 0;
        std::string codec_name_;
    private:
        void InitLog();
        // 打开/重开 x264(重开后强制 IDR 续接,delta 链对端无感)
        bool OpenCodec(uint32_t bitrate);
    };

}

#endif //PX_FFMPEG_ENCODER_H
