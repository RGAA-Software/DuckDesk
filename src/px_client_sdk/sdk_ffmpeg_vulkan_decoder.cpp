#ifdef WIN32

#include "sdk_ffmpeg_vulkan_decoder.h"
#include "ffmpeg_frame_adapter.h"
#include "platform/windows/windows_video_frame.h"
#include "platform/windows/windows_video_resources.h"
#include <iostream>
#include <thread>
#include <fstream>
#include "thunder_sdk.h"
#include "sdk_statistics.h"
#include "px_message.pb.h"
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_common/file.h"
#include "px_common/time_util.h"
#include "px_common/string_util.h"
#include "px_client_sdk/gl/raw_image.h"
#include "sdk_messages.h"

namespace px
{
    enum AVPixelFormat VulkanFFGetFormat(AVCodecContext* context, const enum AVPixelFormat* pixFmts) {
        for (const AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
            if (*p == AV_PIX_FMT_VULKAN)
                return *p;
        }
        return AV_PIX_FMT_NONE; // 如果 Vulkan 不在候选中，直接失败
    }

    enum AVPixelFormat YUV420FFGetFormat(AVCodecContext* context, const enum AVPixelFormat* pixFmts) {
        for (const AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
            if (*p == AV_PIX_FMT_YUV420P)
                return *p;
        }
        return AV_PIX_FMT_NONE;
    }

    enum AVPixelFormat YUV444FFGetFormat(AVCodecContext* context, const enum AVPixelFormat* pixFmts) {
        for (const AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
            if (*p == AV_PIX_FMT_YUV444P)
                return *p;
        }
        return AV_PIX_FMT_NONE; 
    }

    FFmpegVulkanDecoder::FFmpegVulkanDecoder(const std::shared_ptr<ThunderSdk>& sdk, std::shared_ptr<const WindowsVideoResources> resources)
        : VideoDecoder(sdk), resources_(std::move(resources)) {}

    FFmpegVulkanDecoder::~FFmpegVulkanDecoder() {
        Release();
    }

    // img_format:
    // kI420 = 0,
    // kI444 = 1,
    int FFmpegVulkanDecoder::Init(const std::string& mon_name, VideoType codec_type, int width, int height, const std::string& frame,
                                  EImageFormat img_format, bool ignore_hw) {
        if (VideoDecoder::Init(mon_name, codec_type, width, height, frame, img_format, ignore_hw) != 0)
            return AVERROR(EINVAL);
        if (inited_) {
            return 0;
        }
        if (!resources_) return AVERROR(EINVAL);
        monitor_name_ = mon_name;
        img_format_ = img_format;
        frame_width_ = width;
        frame_height_ = height;
        av_log_set_callback([](void* ptr, int level, const char* fmt, va_list vl) {
            char buffer[4096] = {0};
            vsnprintf(buffer, sizeof(buffer), fmt, vl);
            const char* level_str;
            switch (level) {
                case AV_LOG_PANIC: level_str = "PANIC"; break;
                case AV_LOG_FATAL: level_str = "FATAL"; break;
                case AV_LOG_ERROR: level_str = "ERROR"; break;
                case AV_LOG_WARNING: level_str = "WARN"; break;
                case AV_LOG_INFO: level_str = "INFO"; break;
                case AV_LOG_VERBOSE: level_str = "VERBOSE"; break;
                case AV_LOG_DEBUG: level_str = "DEBUG"; break;
                default: level_str = "UNKNOWN"; break;
            }
        });
        av_log_set_level(AV_LOG_WARNING);

        const AVCodec* decoder = NULL;
        void* opaque = NULL;

        this->codec_type_ = codec_type;
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
       
        if (codec_id == AV_CODEC_ID_NONE) {
            LOGI("Unsupported codec type: {}", (int)codec_type);
            return -1;
        }

        if (!InitCodecContext(codec_id)) {
            Release();
            return -2;
        }

        packet_.reset(av_packet_alloc());
        av_frame_ = AllocateAvFrame();
        if (!av_frame_ || !packet_) {
            Release();
            return AVERROR(ENOMEM);
        }
        stop_ = false;
        inited_ = true;
        return 0;
    }

    bool FFmpegVulkanDecoder::InitCodecContext(AVCodecID codec_id) {
        if (!img_format_)
            return false;
        const auto img_format = *img_format_;
        decoder_ = const_cast<AVCodec*>(avcodec_find_decoder(codec_id));
        if (!decoder_) {
            LOGI("can not find decoder for codec_id: {}", (int)codec_id);
            return false;
        }

        decoder_context_.reset(avcodec_alloc_context3(decoder_));
        if (!decoder_context_) {
            LOGE("avcodec_alloc_context3 error");
            return false;
        }

        // Always request low delay decoding
        decoder_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;

        // Allow display of corrupt frames and frames missing references
        decoder_context_->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
        decoder_context_->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;

        if (frame_width_ > 0 && frame_height_ > 0) {
            decoder_context_->width = frame_width_;
            decoder_context_->height = frame_height_;
        }
        // Report decoding errors to allow us to request a key frame
        //
        // With HEVC streams, FFmpeg can drop a frame (hwaccel->start_frame() fails)
        // without telling us. Since we have an infinite GOP length, this causes artifacts
        // on screen that persist for a long time. It's easy to cause this condition
        // by using NVDEC and delaying 100 ms randomly in the render path so the decoder
        // runs out of output buffers.
        decoder_context_->err_recognition = AV_EF_EXPLODE;

        SdkMsgVideoDecodeInit init_msg;
        init_msg.width_ = frame_width_;
        init_msg.height_ = frame_height_;
        init_msg.format_ = img_format;

        if (!ignore_hw_decoder_ && (resources_->decoder_preference == "Auto" || resources_->decoder_preference == "Hardware")) {
            if (!resources_->vulkan_device) return false;
            pix_format_ = decoder_context_->pix_fmt = AV_PIX_FMT_VULKAN;// 表示 解码输出的像素格式
            decoder_context_->get_format = VulkanFFGetFormat;// 是 FFmpeg 解码器在解码初始化阶段调用的回调函数，用来由你（应用层）选择最终的输出像素格式

            decoder_context_->hw_device_ctx = av_buffer_ref(resources_->vulkan_device.get());
            if (!decoder_context_->hw_device_ctx) return false;
            // No threading for HW decode
            decoder_context_->thread_count = 1;
            init_msg.hard_ware_ = true;
            if (AV_CODEC_ID_H264 == codec_id) {
                sdk_stat_->video_decoder_.Update("264(Vulkan)");
            } else if (AV_CODEC_ID_H265 == codec_id) {
                sdk_stat_->video_decoder_.Update("265(Vulkan)");
            }
        }
        else {  //软解码
            auto fnGetPreferredPixelFormat = [](EImageFormat format) -> AVPixelFormat {
                return format == EImageFormat::kI420 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV444P;
            };
            pix_format_ = decoder_context_->pix_fmt = fnGetPreferredPixelFormat(img_format);
            decoder_context_->get_format = img_format_ == EImageFormat::kI420 ? YUV420FFGetFormat : YUV444FFGetFormat;;
            decoder_context_->thread_type = FF_THREAD_SLICE;
            decoder_context_->thread_count = std::min(8, (int)std::thread::hardware_concurrency());
            init_msg.hard_ware_ = false;
        }

        AVDictionary* options = nullptr;
        int err = avcodec_open2(decoder_context_.get(), decoder_, &options);
        av_dict_free(&options);
        if (0 != err) {
            return false;
        }

        SendInitMsg(init_msg);

        return true;
    }

    Result<std::shared_ptr<RawImage>, int> FFmpegVulkanDecoder::Decode(std::span<const std::uint8_t> encoded) {
        std::lock_guard<std::mutex> guard(decode_mtx_);
        if (!decoder_context_ || !av_frame_ || !packet_ || stop_) {
            return TRError(-1);
        }

        auto beg = TimeUtil::GetCurrentTimestamp();

        if (!PrepareDecoderPacket(*packet_, encoded)) return TRError(AVERROR(EINVAL));

        int ret = avcodec_send_packet(decoder_context_.get(), packet_.get());
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
        std::shared_ptr<RawImage> decoded_image = nullptr;
        while (true) {
            ret = avcodec_receive_frame(decoder_context_.get(), av_frame_.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                last_result = has_received_frame ? 0 : ret;
                break;
            }
            else if (ret != 0) {
                LOGE("avcodec_receive_frame error: {}", ret);
                last_result = ret;
                break;
            }
            has_received_frame = true;
            auto width = av_frame_->width;
            auto height = av_frame_->height;

            if (av_frame_->flags & AV_FRAME_FLAG_KEY) {
                LOGI("key frame!!!!!!! AV frame format: {}", (int)av_frame_->format);
            }

            if (av_frame_->format != pix_format_) {
                LOGE("AV frame format: {}, is not {}", av_frame_->format, (int)pix_format_);
                return TRError(-2);
            }

#if 0
            LOGI("Frame format: {} {}",
                av_frame_->format,
                av_get_pix_fmt_name((AVPixelFormat)av_frame_->format));
            if (av_frame_->hw_frames_ctx) {
                const AVHWFramesContext* hwfc = (const AVHWFramesContext*)av_frame_->hw_frames_ctx->data;
                LOGI("HW device type: {}, sw_format: {}",  // 现在 sw_format 就是 Vulkan 图像在主机内存中的对应像素格式
                    av_hwdevice_get_type_name(hwfc->device_ctx->type),
                    av_get_pix_fmt_name((AVPixelFormat)hwfc->sw_format));
            }
            else {
                LOGI("No hw_frames_ctx!\n");
            }
#endif

            auto end = TimeUtil::GetCurrentTimestamp();
            const auto sdk_stat = sdk_stat_;
            const auto monitor_name = monitor_name_;
            const auto image_format = img_format_;
            const auto decode_duration = end - beg;
            sdk_->PostMiscTask([sdk_stat, monitor_name, image_format, decode_duration]() {
                sdk_stat->video_color_.Update(image_format == EImageFormat::kI444 ? "4:4:4" : "4:2:0");
                sdk_stat->AppendDecodeDuration(monitor_name, decode_duration);
            });
            decoded_image = MakeVulkanImage(*av_frame_, resources_->vulkan_device);
            if (!decoded_image) {
                return TRError(AVERROR(ENOMEM));
            }
            decoded_image->full_color_ = img_format_ == EImageFormat::kI444;
            break;
        }
          
        if (decoded_image) {
            return decoded_image;
        }

        return TRError(last_result);
    }

    void FFmpegVulkanDecoder::Release() {
        std::lock_guard guard(decode_mtx_);
        stop_ = true;
        inited_ = false;
        av_frame_.reset();
        packet_.reset();
        decoder_context_.reset();
    }

    bool FFmpegVulkanDecoder::Ready() {
        return inited_;
    }
}

#endif
