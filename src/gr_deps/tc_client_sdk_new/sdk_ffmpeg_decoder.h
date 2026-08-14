//
// Created by RGAA on 2023/8/11.
//

#ifndef SAILFISH_CLIENT_PC_FFMPEG_D3D11VA_DECODER_H
#define SAILFISH_CLIENT_PC_FFMPEG_D3D11VA_DECODER_H

extern "C"
{
    #include <libavcodec/codec.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
    #include <libavutil/log.h>
    #include <libavutil/pixdesc.h>
    #include <libavutil/hwcontext.h>
    #include <libavutil/avassert.h>
}

#ifdef WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif

#include <set>
#include "sdk_video_decoder.h"

namespace tc
{

    class D3D11DeviceWrapper;

    class FFmpegDecoder : public VideoDecoder {
    public:
        explicit FFmpegDecoder(const std::shared_ptr<ThunderSdk>& sdk);
        ~FFmpegDecoder() override;

        int Init(const std::string& mon_name, int codec_type, int width, int height, const std::string& frame, void* surface, int img_format, bool ignore_hw) override;
        Result<std::shared_ptr<RawImage>, int> Decode(const uint8_t* data, int size) override;
        void Release() override;
        bool Ready() override;

        static enum AVPixelFormat ffGetFormat(AVCodecContext* context, const enum AVPixelFormat* pixFmts);

    private:
        int GetAVCodecCapabilities(const AVCodec *codec);
        bool IsHardwareAccelerated();

    private:
        AVCodecContext* decoder_context_ = nullptr;
        AVCodec* decoder_ = nullptr;
        AVPacket* packet_ = nullptr;
        AVFrame* av_frame_ = nullptr;

        AVBufferRef* hw_device_context_ = nullptr;
        AVBufferRef* hw_frames_context_ = nullptr;
        AVCodecHWConfig* hw_decode_config = nullptr;

        AVPixelFormat last_format_ = AV_PIX_FMT_NONE;
        std::shared_ptr<RawImage> decoded_image_ = nullptr;
        std::shared_ptr<D3D11DeviceWrapper> d3d11_wrapper_ = nullptr;
    };

}

#endif //SAILFISH_CLIENT_PC_FFMPEGVIDEODECODER_H
