#pragma once
#ifdef WIN32

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

#include <set>
#include "sdk_video_decoder.h"

namespace tc
{

    class FFmpegVulkanDecoder : public VideoDecoder {
    public:
        explicit FFmpegVulkanDecoder(const std::shared_ptr<ThunderSdk>& sdk);
        ~FFmpegVulkanDecoder() override;

        int Init(const std::string& mon_name, int codec_type, int width, int height, const std::string& frame, void* surface, int img_format, bool ignore_hw) override;
        Result<std::shared_ptr<RawImage>, int> Decode(const uint8_t* data, int size) override;

        void Release() override;
        bool Ready() override;

        static enum AVPixelFormat ffGetFormat(AVCodecContext* context, const enum AVPixelFormat* pixFmts);

    private:
        
        bool InitCodecContext(AVCodecID codec_id);
    private:
        AVCodecContext* decoder_context_ = nullptr;
        AVCodec* decoder_ = nullptr;
        AVPacket* packet_ = nullptr;
        AVFrame* av_frame_ = nullptr;

        AVBufferRef* hw_device_context_ = nullptr;

        AVPixelFormat pix_format_ = AV_PIX_FMT_NONE;
    };

}

#endif
