//
// Created by RGAA on 2023/8/11.
//

#include "sdk_ffmpeg_decoder.h"
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
#ifdef WIN32
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <atlbase.h>
#include "px_common/win32/d3d11_wrapper.h"
#endif

namespace px
{

    FFmpegDecoder::FFmpegDecoder(const std::shared_ptr<ThunderSdk>& sdk, std::shared_ptr<const WindowsVideoResources> resources)
        : VideoDecoder(sdk), d3d11_wrapper_(resources ? resources->d3d11 : nullptr), resources_(std::move(resources)) {}

    FFmpegDecoder::~FFmpegDecoder() {
        Release();
    }
#ifdef WIN32
    static std::string PrintAdapterInfo(ComPtr<ID3D11Device> device) {
        if (!device) return "";

        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = device.As(&dxgiDevice);
        if (FAILED(hr)) {
            LOGE("Failed to get IDXGIDevice");
            return "";
        }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) {
            LOGE("Failed to get IDXGIAdapter");
            return "";
        }

        DXGI_ADAPTER_DESC desc;
        hr = adapter->GetDesc(&desc);
        if (FAILED(hr)) {
            LOGE("Failed to get adapter desc");
            return "";
        }

        std::stringstream ss;
        ss << "VendorId: " << desc.VendorId << ", DeviceId: " << desc.DeviceId << std::endl;
        ss << "SubSysId: " << desc.SubSysId << ", Revision: " << desc.Revision << std::endl;
        ss << "LUID: "<< desc.AdapterLuid.HighPart << ":" << desc.AdapterLuid.LowPart << std::endl;
        return ss.str();
    }
#endif

    // img_format:
    // kI420 = 0,
    // kI444 = 1,
    int FFmpegDecoder::Init(const std::string& mon_name, VideoType codec_type, int width, int height, const std::string& frame,
                            EImageFormat img_format, bool ignore_hw) {
        if (VideoDecoder::Init(mon_name, codec_type, width, height, frame, img_format, ignore_hw) != 0)
            return AVERROR(EINVAL);
        if (inited_) {
            return 0;
        }
        if (!resources_) return AVERROR(EINVAL);
        monitor_name_ = mon_name;
        img_format_ = img_format;

        SdkMsgVideoDecodeInit init_msg;
        init_msg.width_ = width;
        init_msg.height_ = height;
        init_msg.format_ = img_format;

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
            //LOGI("FFMPEG [{}]: {}", level_str, buffer);
        });
        av_log_set_level(AV_LOG_WARNING);

        bool found_target_codec = false;
        const AVCodec* decoder = NULL;
        void* opaque = NULL;

        auto fnGetPreferredPixelFormat = [] (int format) -> AVPixelFormat {
#if 0
            if (videoFormat & VIDEO_FORMAT_MASK_10BIT) {
                return (videoFormat & VIDEO_FORMAT_MASK_YUV444) ?
                       AV_PIX_FMT_YUV444P10 : // 10-bit 3-plane YUV 4:4:4
                       AV_PIX_FMT_P010;       // 10-bit 2-plane YUV 4:2:0
            }
            else {
                return (videoFormat & VIDEO_FORMAT_MASK_YUV444) ?
                       AV_PIX_FMT_YUV444P : // 8-bit 3-plane YUV 4:4:4
                       AV_PIX_FMT_YUV420P;  // 8-bit 3-plane YUV 4:2:0
            }
#endif
            return format == EImageFormat::kI420 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV444P;
        };

#ifdef WIN32
        LOGI("Decoder prefer: {}, ignore hw? {}, has d3d11 device? {}", resources_->decoder_preference,
             ignore_hw_decoder_, d3d11_wrapper_ != nullptr);
#else
        LOGI("Decoder prefer: {}, ignore hw? {}", resources_->decoder_preference, ignore_hw_decoder_);
#endif

        if ((resources_->decoder_preference == "Auto" || resources_->decoder_preference == "Hardware")
            #ifdef WIN32
            && !ignore_hw_decoder_ && d3d11_wrapper_
            #endif
            ) {
            LOGI("Available codecs:");
            while ((decoder = av_codec_iterate(&opaque)) != NULL) {
                if (decoder->type != AVMEDIA_TYPE_VIDEO) {
                    continue;
                }
                if (!av_codec_is_decoder(decoder)) {
                    continue;
                }

                if (false) {
                    LOGI("============================0");
                    for (int i = 0;; i++) {
                        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
                        if (!config) {
                            break;
                        }
                        LOGI(" ==> HW device type: {}, pix format: {}", (int) config->device_type,
                             (int) config->pix_fmt);

                    }
                    LOGI("============================1");
                }


                if (GetAVCodecCapabilities(decoder) & AV_CODEC_CAP_HARDWARE) {
                    continue;
                }

                if (codec_type == VideoType::kNetH264) {
                    if (decoder->id != AV_CODEC_ID_H264) {
                        continue;
                    }
                } else if (codec_type == VideoType::kNetHevc) {
                    if (decoder->id != AV_CODEC_ID_HEVC) {
                        continue;
                    }
                } else {
                    // AV_CODEC_ID_AV1
                }

                for (int i = 0; ; i++) {
                    const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
                    if (!config) {
                        break;
                    }

#ifdef WIN32
                    LOGI(" ==> HW device type: {}, pix format: {}", (int) config->device_type, (int) config->pix_fmt);
                    if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
                        LOGI("Found the D3D11VA, Codec name: {}", decoder->name);
                        if ((img_format == EImageFormat::kI420 && config->pix_fmt == AV_PIX_FMT_D3D11)) {
                            found_target_codec = true;
                                decoder_ = const_cast<AVCodec*>(decoder);
                                hw_decode_config = const_cast<AVCodecHWConfig*>(config);
                            LOGI("D3D11VA support image format: {}",
                                 (img_format == EImageFormat::kI420 ? "YUV420" : "YUV444"));
                            break;
                        } else {
                            LOGW("D3D11VA doesn't support image format: {}",
                                 (img_format == EImageFormat::kI420 ? "YUV420" : "YUV444"));
                        }
                    }
#endif
                    if (found_target_codec) {
                        break;
                    }
                }
            }
        }

        if (decoder_ && found_target_codec) {
            LOGI("Found HW codec, name: {}, id: {}, long name: {}", decoder_->name, (int)decoder_->id, decoder_->long_name);

            this->codec_type_ = codec_type;
            this->frame_width_ = width;
            this->frame_height_ = height;
            AVCodecID codec_id = AV_CODEC_ID_NONE;
            if (codec_type == VideoType::kNetH264) {
                sdk_stat_->video_format_.Update("H264");
            }
            else if (codec_type == VideoType::kNetHevc) {
                sdk_stat_->video_format_.Update("HEVC");
            }

            if (hw_decode_config->pix_fmt == AV_PIX_FMT_D3D11) {
                sdk_stat_->video_decoder_.Update("D3D11VA");
            }
            else if (hw_decode_config->pix_fmt == AV_PIX_FMT_VULKAN) {
                sdk_stat_->video_decoder_.Update("Vulkan");
            }
            else {
                sdk_stat_->video_decoder_.Update("Unknown");
            }

            decoder_context_.reset(avcodec_alloc_context3(decoder_));
            if (!decoder_context_) {
                LOGE("Unable to allocate video decoder context");
                return -1;
            }

            // Always request low delay decoding
            decoder_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;

            // Allow display of corrupt frames and frames missing references
            decoder_context_->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
            decoder_context_->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;

            // Report decoding errors to allow us to request a key frame
            //
            // With HEVC streams, FFmpeg can drop a frame (hwaccel->start_frame() fails)
            // without telling us. Since we have an infinite GOP length, this causes artifacts
            // on screen that persist for a long time. It's easy to cause this condition
            // by using NVDEC and delaying 100 ms randomly in the render path so the decoder
            // runs out of output buffers.
            decoder_context_->err_recognition = AV_EF_EXPLODE;

            // Enable slice multi-threading for software decoding
            if (!IsHardwareAccelerated()) {
                decoder_context_->thread_type = FF_THREAD_SLICE;
                decoder_context_->thread_count = std::min(8, (int)std::thread::hardware_concurrency());
            }
            else {
                // No threading for HW decode
                decoder_context_->thread_count = 1;
            }

            // Setup decoding parameters
            decoder_context_->width = width;
            decoder_context_->height = height;
            decoder_context_->pix_fmt = fnGetPreferredPixelFormat(img_format);
            decoder_context_->get_format = ffGetFormat;

            AVDictionary* options = nullptr;
            decoder_context_->opaque = this;
            int err = avcodec_open2(decoder_context_.get(), decoder_, &options);
            av_dict_free(&options);
            if (err < 0) {
                LOGE("Unable to open decoder for format: {}", img_format);
                return err;
            }

            hw_device_context_ = AvBufferPtr(av_hwdevice_ctx_alloc(hw_decode_config->device_type), AvBufferDeleter{});
            if (!hw_device_context_) {
                LOGE("Failed to create D3D11VA device context");
                return -1;
            }

#ifdef WIN32
            const auto& hw_ctx = *reinterpret_cast<AVHWDeviceContext*>(hw_device_context_->data);
            auto& d3d11ctx = *static_cast<AVD3D11VADeviceContext*>(hw_ctx.hwctx);

            if (!d3d11_wrapper_) {
                LOGE("Don't have d3d11 wrapper, failed to init d3d11va decoder.");
                return -1;
            }
            LOGI("d3d11device adapter uid: {}", d3d11_wrapper_->adapter_uid_);

            auto info = PrintAdapterInfo(d3d11_wrapper_->d3d11_device_);
            LOGI("ORIGIN D3D INFO: {}", info);

            // FFmpeg owns independent COM references while queued frames retain its pool.
            if (FAILED(d3d11_wrapper_->d3d11_device_.CopyTo(&d3d11ctx.device)) ||
                FAILED(d3d11_wrapper_->d3d11_device_context_.CopyTo(&d3d11ctx.device_context))) return AVERROR(ENOMEM);

            decoder_context_->hw_device_ctx = av_buffer_ref(hw_device_context_.get());
            err = av_hwdevice_ctx_init(hw_device_context_.get());
            if (err < 0) {
                LOGE("Failed to initialize D3D11VA device context: {}", err);
                return err;
            }

            ////

            hw_frames_context_ = AvBufferPtr(av_hwframe_ctx_alloc(hw_device_context_.get()), AvBufferDeleter{});
            if (!hw_frames_context_) {
                LOGE("Failed to allocate D3D11VA frame context");
                return -1;
            }

            auto framesContext = (AVHWFramesContext*)hw_frames_context_->data;
            framesContext->format = AV_PIX_FMT_D3D11;
            //if (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) {
            //    framesContext->sw_format = (params->videoFormat & VIDEO_FORMAT_MASK_YUV444) ?
            //                               AV_PIX_FMT_XV30 : AV_PIX_FMT_P010;
            //}
            //else {
            //    framesContext->sw_format = (params->videoFormat & VIDEO_FORMAT_MASK_YUV444) ?
            //                               AV_PIX_FMT_VUYX : AV_PIX_FMT_NV12;
            //}
            // todo: Fix the 444 format
            framesContext->sw_format = img_format == EImageFormat::kI420 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_VUYX;

            // Surfaces must be 16 pixel aligned for H.264 and 128 pixel aligned for everything else
            // https://github.com/FFmpeg/FFmpeg/blob/a234e5cd80224c95a205c1f3e297d8c04a1374c3/libavcodec/dxva2.c#L609-L616
            auto m_TextureAlignment = img_format == EImageFormat::kI420 ? 16 : 128;

            framesContext->width = FFALIGN(width, m_TextureAlignment);
            framesContext->height = FFALIGN(height, m_TextureAlignment);

            // We can have up to 16 reference frames plus a working surface
            framesContext->initial_pool_size = 17;

            auto d3d11vaFramesContext = (AVD3D11VAFramesContext*)framesContext->hwctx;

            d3d11vaFramesContext->BindFlags = D3D11_BIND_DECODER;
            // if (m_BindDecoderOutputTextures) {
            //     // We need to override the default D3D11VA bind flags to bind the textures as a shader resources
            //     d3d11vaFramesContext->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            // }

            err = av_hwframe_ctx_init(hw_frames_context_.get());
            if (err < 0) {
                LOGE("Failed to initialize D3D11VA frame context: {}", err);
                return err;
            }

            D3D11_TEXTURE2D_DESC textureDesc;
            d3d11vaFramesContext->texture_infos->texture->GetDesc(&textureDesc);
#endif
            init_msg.hard_ware_ = true;
        }
        else {
            //
            LOGI("Can't find hardware codec for format: {}, will use software decoder.", img_format);

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

            decoder = const_cast<AVCodec*>(avcodec_find_decoder(codec_id));
            if (!decoder) {
                LOGE("can not find codec");
                return -1;
            }

            decoder_context_.reset(avcodec_alloc_context3(decoder));
            if (decoder_context_ == nullptr) {
                LOGE("Could not alloc video context!");
                return -1;
            }

            auto pix_format = img_format_ == EImageFormat::kI420 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV444P;
            decoder_context_->pix_fmt = pix_format;
            decoder_context_->thread_count = std::min(8, (int)std::thread::hardware_concurrency());
            decoder_context_->thread_type = FF_THREAD_SLICE;

            if (avcodec_open2(decoder_context_.get(), decoder, nullptr) < 0) {
                LOGE("Failed to open decoder");
                Release();
                return -1;
            }

            av_opt_set_int(decoder_context_.get(), "flags", AV_CODEC_FLAG_LOW_DELAY, 0);

            LOGI("Decoder thread count: {}", decoder_context_->thread_count);

            
            init_msg.hard_ware_ = false;
        }

        packet_.reset(av_packet_alloc());
        av_frame_ = AllocateAvFrame();
        if (!packet_ || !av_frame_) { Release(); return AVERROR(ENOMEM); }
        
        inited_ = true;

        SendInitMsg(init_msg);

        return 0;
    }

    enum AVPixelFormat FFmpegDecoder::ffGetFormat(AVCodecContext* context, const enum AVPixelFormat* pixFmts) {
        auto decoder = (FFmpegDecoder*)context->opaque;
        const AVPixelFormat *p;
        AVPixelFormat desiredFmt;

        if (decoder && decoder->hw_decode_config) {
            desiredFmt = decoder->hw_decode_config->pix_fmt;
        }
        // else if (decoder->m_RequiredPixelFormat != AV_PIX_FMT_NONE) {
        //     desiredFmt = decoder->m_RequiredPixelFormat;
        // }
        // else {
        //     desiredFmt = decoder->m_FrontendRenderer->getPreferredPixelFormat(decoder->m_VideoFormat);
        // }

        for (p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
            // Only match our hardware decoding codec or preferred SW pixel
            // format (if not using hardware decoding). It's crucial
            // to override the default get_format() which will try
            // to gracefully fall back to software decode and break us.
            if (*p == desiredFmt/* && decoder->m_BackendRenderer->prepareDecoderContextInGetFormat(context, *p)*/) {
                context->hw_frames_ctx = av_buffer_ref(decoder->hw_frames_context_.get());
                return *p;
            }
        }

        // Failed to match the preferred pixel formats. Try non-preferred pixel format options
        // for non-hwaccel decoders if we didn't have a required pixel format to use.
        // if (decoder->hw_decode_config == nullptr && decoder->m_RequiredPixelFormat == AV_PIX_FMT_NONE) {
        //     for (p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
        //         if (decoder->m_FrontendRenderer->isPixelFormatSupported(decoder->m_VideoFormat, *p) &&
        //             decoder->m_BackendRenderer->prepareDecoderContextInGetFormat(context, *p)) {
        //             return *p;
        //         }
        //     }
        // }

        return AV_PIX_FMT_NONE;
    }

    Result<std::shared_ptr<RawImage>, int> FFmpegDecoder::Decode(std::span<const std::uint8_t> encoded) {
        if (!decoder_context_ || !av_frame_ || stop_) {
            return TRError(-1);
        }
        std::lock_guard<std::mutex> guard(decode_mtx_);

        auto beg = TimeUtil::GetCurrentTimestamp();
        av_frame_unref(av_frame_.get());
        av_packet_unref(packet_.get());

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
            } else if (ret != 0) {
                LOGE("avcodec_receive_frame error: {}", ret);
                last_result = ret;
                break;
            }
            has_received_frame = true;
            auto width = av_frame_->width;
            auto height = av_frame_->height;

            auto x1 = av_frame_->linesize[0];
            auto x2 = av_frame_->linesize[1];
            auto x3 = av_frame_->linesize[2];

            //LOGI("Frame size: {}x{}, x1,2,3 {} {} {}", width, height, x1, x2, x3);
            if (av_frame_->flags & AV_FRAME_FLAG_KEY) {
                LOGI("key frame!!!!!!! AV frame format: {}", av_frame_->format);
            }

            // ONLY D3D11 NOW
            if (av_frame_->format == AV_PIX_FMT_D3D11) {
#ifdef WIN32
                decoded_image = MakeD3D11Image(*av_frame_, d3d11_wrapper_);
                if (!decoded_image) return TRError(AVERROR(EINVAL));
                decoded_image->full_color_ = false;
                auto end = TimeUtil::GetCurrentTimestamp();
                const auto sdk_stat = sdk_stat_;
                const auto monitor_name = monitor_name_;
                const auto decode_duration = end - beg;
                sdk_->PostMiscTask([sdk_stat, monitor_name, decode_duration]() {
                    sdk_stat->video_color_.Update("4:2:0");
                    sdk_stat->AppendDecodeDuration(monitor_name, decode_duration);
                });
#endif
            }
            else if (av_frame_->format == AV_PIX_FMT_VULKAN) {

            }
            else {
                frame_width_ = width;
                frame_height_ = height;
                decoded_image = CopyCpuAvFrame(*av_frame_, decoded_image_);
                if (!decoded_image) return TRError(AVERROR(EINVAL));
                sdk_stat_->video_color_.Update(decoded_image->full_color_ ? "4:4:4" : "4:2:0");

                if (decoded_image_ && !stop_) {
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

                break;
            }
            //
            //av_frame_unref(av_frame_.get());
        }
        //av_packet_unref(packet_.get());

        if (decoded_image) {
            return decoded_image;
        }

        return TRError(last_result);
    }

    void FFmpegDecoder::Release() {
        std::lock_guard guard(decode_mtx_);
        stop_ = true;
        inited_ = false;
        decoded_image_.reset();
        av_frame_.reset();
        packet_.reset();
        decoder_context_.reset();
        hw_frames_context_.reset();
        hw_device_context_.reset();
    }

    bool FFmpegDecoder::Ready() {
        return inited_;
    }

    int FFmpegDecoder::GetAVCodecCapabilities(const AVCodec *codec) {
        int caps = codec->capabilities;
        // There are a bunch of out-of-tree OMX decoder implementations
        // from various SBC manufacturers that all seem to forget to set
        // AV_CODEC_CAP_HARDWARE (probably because the upstream OMX code
        // also doesn't set it). Avoid a false decoder warning on startup
        // by setting it ourselves.
        auto codec_name = std::string(codec->name);
        StringUtil::ToLower(codec_name);
        if (codec_name.rfind("_omx") != std::string::npos) {
            caps |= AV_CODEC_CAP_HARDWARE;
        }
        //if (QString::fromUtf8(codec->name).endsWith("_omx", Qt::CaseInsensitive)) {
        //    caps |= AV_CODEC_CAP_HARDWARE;
        //}
        return caps;
    }


    bool FFmpegDecoder::IsHardwareAccelerated() {
        return hw_decode_config != nullptr ||
               (GetAVCodecCapabilities(decoder_context_->codec) & AV_CODEC_CAP_HARDWARE) != 0;
    }

}
