//
// Null video decoder factory for rtc local multi-track mode.
//
// In encoded-sink mode the real decoding happens in the sdk's own
// FFmpegVulkanDecoder chain, the built-in webrtc decoder would be pure waste
// (software H264 decode of every frame). But VideoReceiveStream still needs a
// decoder: its frame buffer advances only when decoded frames come back, and
// it fires PLI storms otherwise. This factory advertises the same H264 formats
// the OpenH264 template adapter would(so SDP negotiation is unchanged) and
// hands out decoders that instantly return a 2x2 black frame per input frame.
//

#ifndef PX_RTC_DUMMY_DECODER_FACTORY_H
#define PX_RTC_DUMMY_DECODER_FACTORY_H

#include <memory>
#include <vector>
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/include/video_error_codes.h"

namespace px
{

    class RtcNullVideoDecoder : public webrtc::VideoDecoder {
    public:
        bool Configure(const Settings& settings) override {
            return true;
        }

        int32_t Decode(const webrtc::EncodedImage& input_image,
                       bool missing_frames,
                       int64_t render_time_ms) override {
            if (decode_complete_callback_) {
                auto buffer = webrtc::I420Buffer::Create(2, 2);
                webrtc::I420Buffer::SetBlack(buffer.get());
                auto frame = webrtc::VideoFrame::Builder()
                        .set_video_frame_buffer(buffer)
                        .set_timestamp_rtp(input_image.RtpTimestamp())
                        .set_timestamp_ms(render_time_ms)
                        .build();
                decode_complete_callback_->Decoded(frame, absl::nullopt, absl::nullopt);
            }
            return WEBRTC_VIDEO_CODEC_OK;
        }

        int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* callback) override {
            decode_complete_callback_ = callback;
            return WEBRTC_VIDEO_CODEC_OK;
        }

        int32_t Release() override {
            decode_complete_callback_ = nullptr;
            return WEBRTC_VIDEO_CODEC_OK;
        }

        DecoderInfo GetDecoderInfo() const override {
            return DecoderInfo {
                .implementation_name = "godesk-null",
                .is_hardware_accelerated = false,
            };
        }

    private:
        webrtc::DecodedImageCallback* decode_complete_callback_ = nullptr;
    };

    class RtcNullVideoDecoderFactory : public webrtc::VideoDecoderFactory {
    public:
        std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
            // same formats the OpenH264 template adapter advertises, the render
            // side negotiates H264 only anyway
            return webrtc::SupportedH264DecoderCodecs();
        }

        std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat& format) override {
            return std::make_unique<RtcNullVideoDecoder>();
        }
    };

}

#endif //PX_RTC_DUMMY_DECODER_FACTORY_H
