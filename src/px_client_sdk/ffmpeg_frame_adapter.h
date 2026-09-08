#pragma once

#include "gl/raw_image.h"
#include <climits>
#include <cstdint>
#include <cstring>
#include <span>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace px {

inline bool PrepareDecoderPacket(AVPacket& packet, std::span<const std::uint8_t> encoded) {
    av_packet_unref(&packet);
    if (encoded.empty() || encoded.size() > INT_MAX || av_new_packet(&packet, static_cast<int>(encoded.size())) < 0)
        return false;
    // av_new_packet owns storage and supplies FFmpeg's required zero padding.
    std::memcpy(packet.data, encoded.data(), encoded.size());
    return true;
}

inline std::shared_ptr<RawImage> CopyCpuAvFrame(const AVFrame& frame, std::shared_ptr<RawImage>& cached) {
    const bool nv12 = frame.format == AV_PIX_FMT_NV12;
    if (!nv12 && frame.format != AV_PIX_FMT_YUV420P && frame.format != AV_PIX_FMT_YUV444P)
        return {};
    const auto format = frame.format == AV_PIX_FMT_YUV444P ? kRawImageI444 : kRawImageI420;
    if (!cached || cached.use_count() != 1 || cached->img_width != frame.width || cached->img_height != frame.height || cached->Format() != format) {
        cached = RawImage::Make(format, frame.width, frame.height);
    }
    if (!cached)
        return {};
    for (std::size_t plane{}; plane < 3; ++plane) {
        const auto layout = cached->Layout(plane);
        if (!layout)
            return {};
        const auto source_plane = nv12 && plane > 0 ? 1U : plane;
        const auto row_bytes = static_cast<std::int64_t>(layout->stride) * (nv12 && plane > 0 ? 2 : 1);
        const auto stride = static_cast<std::int64_t>(frame.linesize[source_plane]);
        if (!frame.data[source_plane] || stride == 0 || (stride > 0 ? stride : -stride) < row_bytes)
            return {};
        auto destination = cached->MutablePlane(plane);
        for (int row{}; row < layout->rows; ++row) {
            // The borrowed planes belong to the live FFmpeg frame. Negative and
            // padded strides are respected; no borrowed address escapes this call.
            const std::span<const std::uint8_t> source{frame.data[source_plane] + static_cast<std::ptrdiff_t>(row) * stride,
                                                       static_cast<std::size_t>(row_bytes)};
            auto target = destination.subspan(static_cast<std::size_t>(row) * layout->stride, layout->stride);
            if (nv12 && plane > 0) {
                for (std::size_t column{}; column < target.size(); ++column)
                    target[column] = static_cast<char>(source[column * 2 + plane - 1]);
            } else {
                std::memcpy(target.data(), source.data(), target.size());
            }
        }
    }
    return cached;
}

} // namespace px
