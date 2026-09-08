//
// Created by RGAA on 2023/8/11.
//

#ifndef SAILFISH_CLIENT_PC_FFMPEGVIDEODECODER_H
#define SAILFISH_CLIENT_PC_FFMPEGVIDEODECODER_H

extern "C" {

    #include <libavcodec/codec.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
    #include <libavutil/log.h>
}

#include "sdk_video_decoder.h"
#include "ffmpeg_decoder_handles.h"

namespace px
{

    class FFmpegVideoDecoder : public VideoDecoder {
    public:
        explicit FFmpegVideoDecoder(const std::shared_ptr<ThunderSdk>& sdk);
        ~FFmpegVideoDecoder() override;

        int Init(const std::string& mon_name, int codec_type, int width, int height,
            const std::string& frame, int img_format, bool ignore_hw) override;
        Result<std::shared_ptr<RawImage>, int> Decode(std::span<const std::uint8_t> encoded) override;
        void Release() override;
        bool Ready() override;
        void EnableToRGBFormat();

    private:
        void ListCodecs();

    private:
        DecoderContextPtr codec_context{};
        AVCodec* codec = nullptr;
        DecoderPacketPtr packet{};
        AvFramePtr av_frame{};
        AVPixelFormat last_format_ = AV_PIX_FMT_NONE;
        bool cvt_to_rgb_ = false;
        std::shared_ptr<RawImage> decoded_image_ = nullptr;
    };

}

#endif //SAILFISH_CLIENT_PC_FFMPEGVIDEODECODER_H
