#include "sdk_video_decoder_factory.h"
#include "sdk_video_decoder.h"
#include "px_message.pb.h"

namespace px {

std::shared_ptr<VideoDecoder> VideoDecoderFactory::Initialize(std::shared_ptr<VideoDecoder> decoder, const VideoFrame& frame,
                                                              const bool ignore_hardware) {
    if (!decoder)
        return {};
    if (decoder->Init(frame.mon_name(), frame.type(), frame.frame_width(), frame.frame_height(), frame.data(), frame.image_format(),
                      ignore_hardware) == 0) {
        return decoder;
    }
    // Failed initialization can own partial resources; release before fallback.
    decoder->Release();
    return {};
}

} // namespace px
