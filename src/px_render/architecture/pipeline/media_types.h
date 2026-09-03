#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "diagnostics/render_error.h"

namespace px::render {

// Encoded-media APIs (RecordWriter, sockets, codecs) consume octets. Keeping
// the immutable storage as uint8_t avoids reinterpret_cast at every external
// boundary while shared_ptr<const ...> preserves publication immutability.
using ImmutableByteBuffer = std::shared_ptr<const std::vector<std::uint8_t>>;

[[nodiscard]] ImmutableByteBuffer MakeImmutableByteBuffer(
    std::string_view bytes);
[[nodiscard]] std::string ImmutableByteBufferAsString(
    const ImmutableByteBuffer& bytes);

enum class VideoPixelFormat {
    kBgra8,
    kRgba8,
    kNv12,
    kI420,
};

struct FrameIdentity final {
    std::string stream_id;
    std::string monitor_id;
    std::uint64_t frame_index{0};
    std::uint64_t timestamp_us{0};
    std::uint64_t topology_generation{0};
};

// Immutable after publication through shared_ptr<const CapturedVideoFrame>.
class CapturedVideoFrame final {
public:
    [[nodiscard]] static std::expected<CapturedVideoFrame, RenderError> Create(
        FrameIdentity identity,
        std::uint32_t width,
        std::uint32_t height,
        VideoPixelFormat format,
        ImmutableByteBuffer payload);

    [[nodiscard]] const FrameIdentity& Identity() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] VideoPixelFormat Format() const noexcept;
    [[nodiscard]] ImmutableByteBuffer Payload() const noexcept;

private:
    CapturedVideoFrame(FrameIdentity identity,
                       std::uint32_t width,
                       std::uint32_t height,
                       VideoPixelFormat format,
                       ImmutableByteBuffer payload);

    FrameIdentity identity_;
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    VideoPixelFormat format_{VideoPixelFormat::kBgra8};
    ImmutableByteBuffer payload_;
};

struct EncodedVideoFrame final {
    FrameIdentity identity;
    std::string codec;
    std::uint32_t width{0};
    std::uint32_t height{0};
    bool key_frame{false};
    ImmutableByteBuffer payload;
};

struct CapturedAudioFrame final {
    std::string stream_id;
    std::uint64_t timestamp_us{0};
    std::uint32_t sample_rate_hz{0};
    std::uint16_t channels{0};
    std::uint16_t bits_per_sample{0};
    ImmutableByteBuffer payload;
};

struct EncodedAudioFrame final {
    std::string stream_id;
    std::uint64_t timestamp_us{0};
    std::string codec;
    std::uint32_t samples{0};
    std::uint16_t channels{0};
    std::uint16_t bits_per_sample{0};
    std::uint32_t frame_size{0};
    ImmutableByteBuffer payload;
};

struct MediaClientConnected final {
    std::string visitor_device_id;
    std::string stream_id;
    std::string transport;
};

struct MediaClientDisconnected final {
    std::string visitor_device_id;
    std::string stream_id;
    std::string transport;
};

}  // namespace px::render
