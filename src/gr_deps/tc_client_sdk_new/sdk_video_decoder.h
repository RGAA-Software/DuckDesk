//
// Created by RGAA on 2024/1/26.
//

#ifndef TC_CLIENT_ANDROID_VIDEO_DECODER_H
#define TC_CLIENT_ANDROID_VIDEO_DECODER_H

#include <memory>
#include <functional>
#include <mutex>
#include "tc_common_new/expected.h"
#include "sdk_messages.h"

namespace tc
{
    class Data;
    class RawImage;
    class SdkStatistics;
    class ThunderSdk;

    class VideoDecoder {
    public:
        explicit VideoDecoder(const std::shared_ptr<ThunderSdk>& sdk);
        virtual ~VideoDecoder();

        virtual int Init(const std::string& mon_name, int codec_type, int width, int height, const std::string& frame, void* surface, int img_format, bool ignore_hw);
        virtual Result<std::shared_ptr<RawImage>, int> Decode(const std::shared_ptr<Data>& frame);
        virtual Result<std::shared_ptr<RawImage>, int> Decode(const std::string& frame);
        virtual Result<std::shared_ptr<RawImage>, int> Decode(const uint8_t* data, int size) = 0;
        virtual void Release();
        virtual bool NeedReConstruct(int codec_type, int width, int height, int img_format);
        virtual bool Ready() = 0;
        void SendInitMsg(SdkMsgVideoDecodeInit msg);
    protected:
        bool inited_ = false;
        int codec_type_ = -1;
        int frame_width_ = 0;
        int frame_height_ = 0;
        int img_format_ = -1;

        bool stop_ = false;
        std::mutex decode_mtx_;
        std::string monitor_name_;
        SdkStatistics* sdk_stat_ = nullptr;
        std::shared_ptr<ThunderSdk> sdk_ = nullptr;
        bool ignore_hw_decoder_ = false;
    };

}

#endif //TC_CLIENT_ANDROID_VIDEO_DECODER_H
