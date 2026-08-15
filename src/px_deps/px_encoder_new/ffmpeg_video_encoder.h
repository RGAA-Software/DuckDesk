//
// Created by RGAA on 2023/12/19.
//

#ifndef TC_APPLICATION_FFMPEG_VIDEO_ENCODER_H
#define TC_APPLICATION_FFMPEG_VIDEO_ENCODER_H

#include "video_encoder.h"

extern "C" {
    #include "libavcodec/avcodec.h"
}

namespace tc
{
    class Data;

    class FFmpegVideoEncoder : public VideoEncoder {
    public:
        FFmpegVideoEncoder(const std::shared_ptr<MessageNotifier>& msg_notifier, const EncoderFeature& encoder_feature);
        ~FFmpegVideoEncoder() override;

        bool Initialize(const tc::EncoderConfig& config) override;
        void Encode(const std::shared_ptr<Image>& i420_image, uint64_t frame_index) override;
        //void Encode(uint64_t handle, uint64_t frame_index) override;
        void Exit() override;

    private:
        void ListCodecs();

    private:
        AVCodecContext* context_ = nullptr;
        AVFrame* frame_ = nullptr;
        AVPacket* packet_ = nullptr;
        std::shared_ptr<Data> capture_data_ = nullptr;
    };

}

#endif //TC_APPLICATION_FFMPEG_VIDEO_ENCODER_H
