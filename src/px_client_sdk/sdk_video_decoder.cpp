//
// Created by RGAA on 2024/1/26.
//

#include "sdk_video_decoder.h"
#include "px_common/data.h"
#include "sdk_statistics.h"
#include "thunder_sdk.h"
#include "px_common/message_notifier.h"

namespace px
{

    VideoDecoder::VideoDecoder(const std::shared_ptr<ThunderSdk>& sdk) {
        sdk_ = sdk;
        sdk_stat_ = SdkStatistics::Instance();
    }

    VideoDecoder::~VideoDecoder() {

    }

    int VideoDecoder::Init(const std::string& mon_name, VideoType codec_type, int width, int height, const std::string& frame,
                           EImageFormat img_format, bool ignore_hw) {
        if ((codec_type != VideoType::kNetH264 && codec_type != VideoType::kNetHevc) ||
            (img_format != EImageFormat::kI420 && img_format != EImageFormat::kI444))
            return -1;
        configured_codec_type_ = codec_type;
        configured_width_ = width;
        configured_height_ = height;
        configured_img_format_ = img_format;
        ignore_hw_decoder_ = ignore_hw;
        return 0;
    }

    Result<std::shared_ptr<RawImage>, int> VideoDecoder::Decode(const std::shared_ptr<Data>& frame) {
        if (!frame) return TRError(-1);
        return Decode(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(frame->Bytes().data()), frame->Bytes().size()});
    }

    Result<std::shared_ptr<RawImage>, int> VideoDecoder::Decode(const std::string& frame) {
        return Decode(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(frame.data()), frame.size()});
    }

    void VideoDecoder::Release() {

    }

    bool VideoDecoder::RefreshOutput() {
        return false;
    }

    bool VideoDecoder::NeedReConstruct(VideoType codec_type, int width, int height, EImageFormat img_format) {
        // Decoded dimensions can be codec-aligned (for example 1279 -> 1280).
        // Reconfiguration follows the encoded stream metadata captured at Init.
        return codec_type != configured_codec_type_ || width != configured_width_ || height != configured_height_ ||
               img_format != configured_img_format_;
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
