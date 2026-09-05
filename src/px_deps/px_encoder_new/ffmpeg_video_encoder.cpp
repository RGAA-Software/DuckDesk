//
// Created by RGAA on 2023/12/19.
//

#include "ffmpeg_video_encoder.h"

#include <libyuv.h>

#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "px_common_new/win32/d3d_debug_helper.h"
#include "px_common_new/file.h"
#include "px_common_new/time_util.h"

#include <Winerror.h>

namespace px
{

    FFmpegVideoEncoder::FFmpegVideoEncoder(const std::shared_ptr<MessageNotifier>& msg_notifier, const EncoderFeature& encoder_feature)
        : VideoEncoder(msg_notifier, encoder_feature) {

    }

    FFmpegVideoEncoder::~FFmpegVideoEncoder() {

    }

    bool FFmpegVideoEncoder::Initialize(const px::EncoderConfig& encoder_config) {
        VideoEncoder::Initialize(encoder_config);
        ListCodecs();

        auto encoder_id = encoder_config.codec_type == EVideoCodecType::kHEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
        const AVCodec* encoder = avcodec_find_encoder(encoder_id);
//        const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");

        context_ = avcodec_alloc_context3(encoder);
        if (!context_) {
            LOGE("avcodec_alloc_context3 error!");
            return false;
        }
        context_->width = this->out_width_;
        context_->height = this->out_height_;
        context_->time_base = { 1, this->refresh_rate_ };
        context_->framerate = { this->refresh_rate_, 1};
        context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        context_->pix_fmt = AV_PIX_FMT_YUV420P;
        //context_->thread_count = std::min((int)std::thread::hardware_concurrency()/2, X265_MAX_FRAME_THREADS);
        context_->thread_count = std::min(16, (int)std::thread::hardware_concurrency());
        context_->thread_type = FF_THREAD_SLICE;
        context_->gop_size = encoder_config.gop_size;
        context_->max_b_frames = 0;
        //context_->bit_rate = 10000000; // 10Mbps

        if(-1 == encoder_config.gop_size) {
            //context_->gop_size = 180;
        }
        context_->bit_rate = encoder_config.bitrate;

        LOGI("ffmpeg encoder config:");
        LOGI("bitrate: {}", context_->bit_rate);
        LOGI("format: {}", (encoder_config.codec_type == EVideoCodecType::kHEVC ? "HEVC" : "H264"));
        LOGI("refresh rate(fps): {}", this->refresh_rate_);
        LOGI("thread count: {}", context_->thread_count);
        LOGI("gop size: {}", context_->gop_size);

        AVDictionary* param = nullptr;
        if(encoder_id == AV_CODEC_ID_H264) {
            //av_dict_set(&param, "preset", "superfast",   0);
            av_dict_set(&param, "preset", "ultrafast",   0);
            av_dict_set(&param, "tune",   "zerolatency", 0);
            av_dict_set(&param, "crf", "23", 0);
            av_dict_set(&param, "forced-idr", "1", 0);
        }
        if(encoder_id == AV_CODEC_ID_H265) {
            av_dict_set(&param, "x265-params", "qp=20", 0);
            av_dict_set(&param, "preset", "ultrafast", 0);
            av_dict_set(&param, "tune", "zero-latency", 0);
        }

        auto ret = avcodec_open2(context_, encoder, &param);
        if (ret != 0) {
            LOGE("avcodec_open2 error : {}", ret);
            return false;
        }

        frame_ = av_frame_alloc();
        frame_->width = context_->width;
        frame_->height = context_->height;
        frame_->format = context_->pix_fmt;

        av_frame_get_buffer(frame_, 0);
        packet_ = av_packet_alloc();

        LOGI("Line 1: {} 2: {} 3: {}", frame_->linesize[0], frame_->linesize[1], frame_->linesize[2]);
        return true;
    }

    void FFmpegVideoEncoder::Encode(const std::shared_ptr<Image>& i420_image, uint64_t frame_index) {
        //
        auto img_width = i420_image->width;
        auto img_height = i420_image->height;
        auto i420_data = i420_image->data;

        // re-create when width/height changed
        // todo

        frame_->pts = (int64_t)frame_index;

        if (insert_idr_) {
            insert_idr_ = false;
            frame_->flags |= AV_FRAME_FLAG_KEY;
            frame_->pict_type = AV_PICTURE_TYPE_I;
        } else {
            frame_->flags &= ~AV_FRAME_FLAG_KEY;
            frame_->pict_type = AV_PICTURE_TYPE_NONE;
        }

        int y_size =  img_width * img_height;
        int uv_size = img_width * img_height / 4;
        memcpy(frame_->data[0], i420_data->Bytes().data(), y_size);
        memcpy(frame_->data[1], i420_data->Bytes().data() + y_size, uv_size);
        memcpy(frame_->data[2], i420_data->Bytes().data() + y_size + uv_size, uv_size);

        int send_result = avcodec_send_frame(context_, frame_);
        while (send_result >= 0) {
            int receiveResult = avcodec_receive_packet(context_, packet_);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }

            //LOGI("Packet frame is key: {}", (packet_->flags & AV_PKT_FLAG_KEY));
            auto encoded_data =
                Data::Copy(std::span<const char>{reinterpret_cast<const char*>(packet_->data), static_cast<std::size_t>(packet_->size)});
            if (encoder_callback_) {
                auto image = Image::Make(encoded_data, img_width, img_height, 3);
                encoder_callback_(image, frame_index, false);
            }

            av_packet_unref(packet_);
        }
    }

//    void FFmpegVideoEncoder::Encode(uint64_t handle, uint64_t frame_index) {
//        auto beg = TimeUtil::GetCurrentTimestamp();
//        ComPtr<ID3D11Texture2D> shared_texture;
//        shared_texture = OpenSharedTexture(reinterpret_cast<HANDLE>(handle));
//        if (!shared_texture) {
//            LOGE("OpenSharedTexture failed.");
//            return;
//        }
//        D3D11_TEXTURE2D_DESC desc;
//        shared_texture->GetDesc(&desc);
//
//        if (!CopyID3D11Texture2D(shared_texture)) {
//            LOGE("Copy texture failed!");
//            return;
//        }
//
//        CComPtr<IDXGISurface> staging_surface = nullptr;
//        auto hr = texture2d_->QueryInterface(IID_PPV_ARGS(&staging_surface));
//        if (FAILED(hr)) {
//            LOGE("!QueryInterface(IDXGISurface) err");
//            return;
//        }
//        // E_OUTOFMEMORY
//        DXGI_MAPPED_RECT mapped_rect{};
//        hr = staging_surface->Map(&mapped_rect, DXGI_MAP_READ);
//        if (FAILED(hr)) {
//            LOGE("!Map(IDXGISurface) 0x{:x}", (uint32_t)hr);
//            return;
//        }
//        auto defer = Defer::Make([staging_surface]() {
//            staging_surface->Unmap();
//        });
//
//        // Copy rgba
//        EnsureRawImage(mapped_rect.Pitch, desc.Height);
//        CopyToRawImage(mapped_rect.pBits, mapped_rect.Pitch, desc.Height);
//
//        int width = desc.Width;
//        int height = desc.Height;
//        int target_data_size = 1.5 * width * height;
//        if (!capture_data_ || capture_data_->Size() != target_data_size) {
//            capture_data_ = Data::Allocate( target_data_size);
//        }
//        size_t pixel_size = width * height;
//
//        const int uv_stride = width >> 1;
//        auto y = (uint8_t*)capture_data_->MutableBytes().data();
//        auto u = y + pixel_size;
//        auto v = u + (pixel_size >> 2);
//
//        if (DXGI_FORMAT_B8G8R8A8_UNORM == desc.Format) {
//            libyuv::ARGBToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
//        } else if (DXGI_FORMAT_R8G8B8A8_UNORM == desc.Format) {
//            libyuv::ABGRToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
//        } else {
//            libyuv::ARGBToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
//        }
//        //LOGI("Map & convert: {}ms", (TimeUtil::GetCurrentTimestamp()-beg));
//
//        // Copy YUV
//        {
//            std::lock_guard<std::mutex> guard(raw_image_yuv_mtx_);
//            raw_image_yuv_ = Image::Make(capture_data_, desc.Width, desc.Height, RawImageType::kI420);
//        }
//
//        beg = TimeUtil::GetCurrentTimestamp();
//        auto image = Image::Make(capture_data_, width, height, 3);
//        this->Encode(image, frame_index);
//        //LOGI("Encode: {}ms", (TimeUtil::GetCurrentTimestamp()-beg));
//    }

    void FFmpegVideoEncoder::Exit() {
        VideoEncoder::Exit();
    }

    void FFmpegVideoEncoder::ListCodecs() {
        const AVCodec *codec = nullptr;
        void *opaque = nullptr;

        //LOGI("Available codecs:");
        while ((codec = av_codec_iterate(&opaque)) != nullptr) {
            if (codec->type == AVMEDIA_TYPE_VIDEO || codec->type == AVMEDIA_TYPE_AUDIO) {
                if (av_codec_is_encoder(codec)) {
                    //LOGI("Encoder: {}", codec->name);
                }
                if (av_codec_is_decoder(codec)) {
                    //LOGI("Decoder: {}", codec->name);
                }
            }
        }
    }

}
