//
// Created by RGAA on 2023/8/11.
//

#include "sdk_ffmpeg_soft_decoder.h"
#include "ffmpeg_frame_adapter.h"

#include "px_common/data.h"
#include "px_message.pb.h"
#include "px_common/log.h"
#include "px_client_sdk/gl/raw_image.h"
#include "px_common/time_util.h"
#include "px_common/file.h"
#include "px_common/folder_util.h"
#include "px_common/string_util.h"
#if 000
#include <libyuv.h>
#endif
#include <iostream>
#include <thread>
#include "thunder_sdk.h"
#include "sdk_statistics.h"

namespace px
{

    FFmpegVideoDecoder::FFmpegVideoDecoder(const std::shared_ptr<ThunderSdk>& sdk) : VideoDecoder(sdk) {

    }

    FFmpegVideoDecoder::~FFmpegVideoDecoder() {
        Release();
    }

    int FFmpegVideoDecoder::Init(const std::string& mon_name, int codec_type, int width, int height,
            const std::string& frame, int img_format, bool ignore_hw) {
        VideoDecoder::Init(mon_name, codec_type, width, height, frame, img_format, ignore_hw);
        if (inited_) {
            return 0;
        }
        monitor_name_ = mon_name;
        img_format_ = img_format;
        av_log_set_level(AV_LOG_INFO);
        av_log_set_callback([](void* ptr, int level, const char* fmt, va_list vl) {
        #if defined(__ANDROID__)
            #if defined(ENABLE_FFMPEG_LOG)
            if (level == AV_LOG_ERROR) {
                __android_log_vprint(ANDROID_LOG_ERROR, "FFmpeg", fmt, vl);
            }
            else if (level == AV_LOG_WARNING) {
                __android_log_vprint(ANDROID_LOG_WARN, "FFmpeg", fmt, vl);
            }
            else {
                __android_log_vprint(ANDROID_LOG_INFO, "FFmpeg", fmt, vl);
            }
            #endif
        #else
            #if defined(ENABLE_FFMPEG_LOG)
            vprintf(fmt, vl);
            #endif
        #endif
        });

        // ListCodecs();

        auto format_num = [](int val) -> int {
            auto t = val % 2;
            return val + t;
        };

        this->codec_type_ = codec_type;
        this->frame_width_ = width;//format_num(width);
        this->frame_height_ = height;//format_num(height);
        AVCodecID codec_id = AV_CODEC_ID_NONE;
        if (codec_type == VideoType::kNetH264) {
            codec_id = AV_CODEC_ID_H264;
            sdk_stat_->video_format_.Update("H264");
            sdk_stat_->video_decoder_.Update("X264");
        }
        else if (codec_type == VideoType::kNetHevc) {
            codec_id = AV_CODEC_ID_H265;
            sdk_stat_->video_format_.Update("HEVC");
            sdk_stat_->video_decoder_.Update("X265");
        }
        /*else if (codec_type == VideoType::kVp9) {
            codec_id = AV_CODEC_ID_VP9;
        }*/
        if (codec_id == AV_CODEC_ID_NONE) {
            std::cout << "CodecId not find : " << codec_type << std::endl;
            return -1;
        }

        codec = const_cast<AVCodec*>(avcodec_find_decoder(codec_id));
        if (!codec) {
            LOGE("can not find codec");
            return -1;
        }

        codec_context.reset(avcodec_alloc_context3(codec));
        if (codec_context == NULL) {
            LOGE("Could not alloc video context!");
            return -1;
        }

        codec_context->thread_count = std::min(8, (int)std::thread::hardware_concurrency());
        codec_context->thread_type = FF_THREAD_SLICE;

        if (avcodec_open2(codec_context.get(), codec, NULL) < 0) {
            LOGE("Failed to open decoder");
            Release();
            return -1;
        }

        av_opt_set_int(codec_context.get(), "flags", AV_CODEC_FLAG_LOW_DELAY, 0);

        LOGI("Decoder thread count: {}", codec_context->thread_count);

        packet.reset(av_packet_alloc());
        av_frame = AllocateAvFrame();
        if (!packet || !av_frame) { Release(); return AVERROR(ENOMEM); }


        inited_ = true;

        return 0;
    }

    void FFmpegVideoDecoder::ListCodecs() {
        const AVCodec *codec = NULL;
        void *opaque = NULL;

        LOGI("Available codecs:");
        while ((codec = av_codec_iterate(&opaque)) != NULL) {
            if (codec->type == AVMEDIA_TYPE_VIDEO || codec->type == AVMEDIA_TYPE_AUDIO) {
                if (av_codec_is_encoder(codec)) {
                    //LOGI("Encoder: {}", codec->name);
                }
                if (av_codec_is_decoder(codec)) {
                    LOGI("Decoder: {}", codec->name);
                }
            }
        }
    }
#if 000
    static void I420ToRGB24(unsigned char* yuv_data, unsigned char* rgb24, int width, int height) {
        unsigned char* y = yuv_data;
        unsigned char* u = &yuv_data[width * height];
        unsigned char* v = &yuv_data[width * height * 5 / 4];
        libyuv::I420ToRGB24(y, width, u, width / 2, v, width / 2,
                            rgb24,
                            width * 3, width, height);
    }
#endif

    Result<std::shared_ptr<RawImage>, int> FFmpegVideoDecoder::Decode(std::span<const std::uint8_t> encoded) {
        if (!codec_context || !av_frame || stop_) {
            return TRError(-1);
        }
        std::lock_guard<std::mutex> guard(decode_mtx_);

        auto beg = TimeUtil::GetCurrentTimestamp();
        av_frame_unref(av_frame.get());

        if (!PrepareDecoderPacket(*packet, encoded)) return TRError(AVERROR(EINVAL));

        int ret = avcodec_send_packet(codec_context.get(), packet.get());
        if (ret == AVERROR(EAGAIN)) {
            LOGW("EAGAIN...");
            return TRError(0);
        }
        else if (ret != 0) {
            LOGE("avcodec_send_packet err: {}", ret);
            return TRError(ret);
        }

        bool has_received_frame = false;
        auto last_result = 0;
        while (true) {
            ret = avcodec_receive_frame(codec_context.get(), av_frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                last_result = has_received_frame ? 0 : ret;
                break;
            } else if (ret != 0) {
                LOGI("avcodec_receive_frame error: {}", ret);
                last_result = ret;
                break;
            }
            has_received_frame = true;

            auto width = av_frame->width;
            auto height = av_frame->height;

            auto x1 = av_frame->linesize[0];
            auto x2 = av_frame->linesize[1];
            auto x3 = av_frame->linesize[2];
            //width = x1;
            //LOGI("frame width: {}, x1: {}", av_frame->width, x1);
           
            bool format_change = false;
            auto format = (AVPixelFormat)codec_context->pix_fmt;
            if (last_format_ != format) {
                LOGI("format : {} change to : {}", (int)last_format_, (int)format);
                last_format_ = format;
                format_change = true;
            }
     
            frame_width_ = width;
            frame_height_ = height;
            const auto image = CopyCpuAvFrame(*av_frame, decoded_image_);
            if (!image) return TRError(AVERROR(EINVAL));
            sdk_stat_->video_color_.Update(image->full_color_ ? "4:4:4" : "4:2:0");

            if (decoded_image_ && !stop_) {
                if (cvt_to_rgb_) {
#if 000
                    auto rgb = RawImage::MakeRGB(nullptr, frame_width_ * frame_height_ * 3,
                                                 frame_width_, frame_height_);
                    auto rgb_data = rgb->Data();
                    auto yuv_data = decoded_image_->Data();
                    I420ToRGB24((uint8_t *) yuv_data, (uint8_t *) rgb_data, frame_width_,
                                frame_height_);
                    cbk(rgb);
                    return 0;
#endif
                }
                else {
                    auto end = TimeUtil::GetCurrentTimestamp();
                    const auto sdk_stat = sdk_stat_;
                    const auto monitor_name = monitor_name_;
                    const auto decode_duration = end - beg;
                    sdk_->PostMiscTask([sdk_stat, monitor_name, decode_duration]() {
                        sdk_stat->AppendDecodeDuration(monitor_name, decode_duration);
                    });
                    //LOGI("FFmpeg decode YUV420p(I420) used : {}ms, {}x{}", (end-beg), frame_width_, frame_height_);
                    return decoded_image_;
                }
            }

            //
            av_frame_unref(av_frame.get());
        }
        av_packet_unref(packet.get());
        return TRError(last_result);
    }

    void FFmpegVideoDecoder::Release() {
        std::lock_guard guard(decode_mtx_);
        stop_ = true;
        inited_ = false;
        decoded_image_.reset();
        av_frame.reset();
        packet.reset();
        codec_context.reset();
    }

    bool FFmpegVideoDecoder::Ready() {
        return inited_;
    }

    void FFmpegVideoDecoder::EnableToRGBFormat() {
        this->cvt_to_rgb_ = true;
    }
}
