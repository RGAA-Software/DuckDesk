//
// Created by RGAA on 2024/1/26.
//

#include "sdk_video_decoder.h"
#include "tc_common_new/data.h"
#include "sdk_statistics.h"
#include "thunder_sdk.h"
#include "tc_common_new/message_notifier.h"

namespace tc
{

    VideoDecoder::VideoDecoder(const std::shared_ptr<ThunderSdk>& sdk) {
        sdk_ = sdk;
        sdk_stat_ = SdkStatistics::Instance();
    }

    VideoDecoder::~VideoDecoder() {

    }

    int VideoDecoder::Init(const std::string& mon_name, int codec_type, int width, int height, const std::string& frame, void* surface, int img_format, bool ignore_hw) {
        ignore_hw_decoder_ = ignore_hw;
        return 0;
    }

    Result<std::shared_ptr<RawImage>, int> VideoDecoder::Decode(const std::shared_ptr<Data>& frame) {
        return this->Decode((uint8_t*)frame->CStr(), frame->Size());
    }

    Result<std::shared_ptr<RawImage>, int> VideoDecoder::Decode(const std::string& frame) {
        return this->Decode((uint8_t*)frame.data(), frame.size());
    }

    void VideoDecoder::Release() {

    }

    bool VideoDecoder::NeedReConstruct(int codec_type, int width, int height, int img_format) {
        // for Windows.
        return codec_type != this->codec_type_ || width != this->frame_width_ || height != this->frame_height_ || img_format != this->img_format_;
    }

    void VideoDecoder::SendInitMsg(SdkMsgVideoDecodeInit msg) {
        if (!sdk_) {
            return;
        }
        auto msg_notifier =  sdk_->GetMessageNotifier();
        if (!msg_notifier) {
            return;
        }
        msg_notifier->SendAppMessage(msg);
    }
}
