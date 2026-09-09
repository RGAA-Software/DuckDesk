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
#include "ffmpeg_decoder_handles.h"
#include "av_buffer_ref.h"

namespace px
{

    class D3D11DeviceWrapper;
    struct WindowsVideoResources;

    class FFmpegDecoder : public VideoDecoder {
    public:
        FFmpegDecoder(const std::shared_ptr<ThunderSdk>& sdk, std::shared_ptr<const WindowsVideoResources> resources);
        ~FFmpegDecoder() override;

        int Init(const std::string& mon_name, VideoType codec_type, int width, int height, const std::string& frame, EImageFormat img_format,
                 bool ignore_hw) override;
        Result<std::shared_ptr<RawImage>, int> Decode(std::span<const std::uint8_t> encoded) override;
        void Release() override;
        bool Ready() override;

        static enum AVPixelFormat ffGetFormat(AVCodecContext* context, const enum AVPixelFormat* pixFmts);

    private:
        int GetAVCodecCapabilities(const AVCodec *codec);
        bool IsHardwareAccelerated();

    private:
        DecoderContextPtr decoder_context_{};
        AVCodec* decoder_ = nullptr;
        DecoderPacketPtr packet_{};
        AvFramePtr av_frame_{};

        AvBufferPtr hw_device_context_{};
        AvBufferPtr hw_frames_context_{};
        AVCodecHWConfig* hw_decode_config = nullptr;

        AVPixelFormat last_format_ = AV_PIX_FMT_NONE;
        std::shared_ptr<RawImage> decoded_image_ = nullptr;
        std::shared_ptr<D3D11DeviceWrapper> d3d11_wrapper_ = nullptr;
        const std::shared_ptr<const WindowsVideoResources> resources_{};
    };

}

#endif //SAILFISH_CLIENT_PC_FFMPEGVIDEODECODER_H
