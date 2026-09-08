#pragma once

#include <memory>

extern "C" {
#include <libavutil/buffer.h>
}

namespace px {
struct AvBufferDeleter final {
    void operator()(AVBufferRef* buffer) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) FFmpeg deallocation boundary.
        av_buffer_unref(&buffer);
    }
};

using AvBufferPtr = std::shared_ptr<AVBufferRef>;

inline AvBufferPtr CloneAvBuffer(const AVBufferRef& buffer) {
    return AvBufferPtr(av_buffer_ref(&buffer), AvBufferDeleter{});
}
} // namespace px
