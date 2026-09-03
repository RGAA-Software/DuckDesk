#include "pipeline/media_types.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace px::render {

ImmutableByteBuffer MakeImmutableByteBuffer(const std::string_view bytes) {
    auto payload = std::make_shared<std::vector<std::uint8_t>>();
    payload->reserve(bytes.size());
    for (const auto value : bytes) {
        payload->push_back(static_cast<std::uint8_t>(value));
    }
    return payload;
}

std::string ImmutableByteBufferAsString(const ImmutableByteBuffer& bytes) {
    if (!bytes) {
        return {};
    }
    std::string result;
    result.reserve(bytes->size());
    std::ranges::transform(
        *bytes, std::back_inserter(result),
        [](const std::uint8_t byte) { return static_cast<char>(byte); });
    return result;
}

std::expected<CapturedVideoFrame, RenderError> CapturedVideoFrame::Create(
    FrameIdentity identity,
    const std::uint32_t width,
    const std::uint32_t height,
    const VideoPixelFormat format,
    ImmutableByteBuffer payload) {
    if (identity.stream_id.empty() || width == 0 || height == 0 ||
        !payload || payload->empty()) {
        return std::unexpected(RenderError{
            .code = RenderErrorCode::kPipelineInvalidFrame,
            .component = "video_pipeline",
            .operation = "create_captured_frame",
            .stage = "ingress",
            .reason = "missing identity, dimensions, or payload",
            .recoverable = true,
        });
    }
    return CapturedVideoFrame(
        std::move(identity), width, height, format, std::move(payload));
}

CapturedVideoFrame::CapturedVideoFrame(
    FrameIdentity identity,
    const std::uint32_t width,
    const std::uint32_t height,
    const VideoPixelFormat format,
    ImmutableByteBuffer payload)
    : identity_(std::move(identity)),
      width_(width),
      height_(height),
      format_(format),
      payload_(std::move(payload)) {}

const FrameIdentity& CapturedVideoFrame::Identity() const noexcept {
    return identity_;
}

std::uint32_t CapturedVideoFrame::Width() const noexcept {
    return width_;
}

std::uint32_t CapturedVideoFrame::Height() const noexcept {
    return height_;
}

VideoPixelFormat CapturedVideoFrame::Format() const noexcept {
    return format_;
}

ImmutableByteBuffer CapturedVideoFrame::Payload() const noexcept {
    return payload_;
}

}  // namespace px::render
