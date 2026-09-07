#pragma once

#include <memory>

extern "C" {
#include <libavutil/frame.h>
}

namespace px {

struct AvFrameDeleter final {
    void operator()(AVFrame* frame) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) FFmpeg deallocation boundary; never retained.
        av_frame_free(&frame);
    }
};

using AvFramePtr = std::shared_ptr<AVFrame>;

inline AvFramePtr AllocateAvFrame() {
    return AvFramePtr(av_frame_alloc(), AvFrameDeleter{});
}

// The descriptor and metadata are independent; FFmpeg references the backing
// buffers/hardware context. The decoder may unref, reuse or destroy its source.
inline AvFramePtr CloneAvFrame(const AVFrame& source) {
    return AvFramePtr(av_frame_clone(&source), AvFrameDeleter{});
}

} // namespace px
