#pragma once
#include "av_frame_ref.h"
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace px {
struct DecoderContextDeleter final {
    void operator()(AVCodecContext* context) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) FFmpeg destruction boundary.
        avcodec_free_context(&context);
    }
};
struct DecoderPacketDeleter final {
    void operator()(AVPacket* packet) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) FFmpeg destruction boundary.
        av_packet_free(&packet);
    }
};
using DecoderContextPtr = std::unique_ptr<AVCodecContext, DecoderContextDeleter>;
using DecoderPacketPtr = std::unique_ptr<AVPacket, DecoderPacketDeleter>;
} // namespace px
