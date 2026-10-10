#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "media_transport/video_stream.h"
#include "px_message.pb.h"

namespace px {

// Receiver-local evidence, never inferred from a debug string or untrusted protobuf fields.
struct VideoReferenceDependency final {
    media::VideoFrameKind kind{media::VideoFrameKind::kPredicted};
    std::optional<std::uint64_t> preceding_frame_index{};
};

struct EncodedVideoDelivery final {
    std::shared_ptr<Message> message{};
    std::optional<VideoReferenceDependency> dependency{};
};

[[nodiscard]] EncodedVideoDelivery MakeReassembledVideoDelivery(const media::VideoFrame& frame);

// The last frame must come from the decode queue, not from the network receiver: queued frames can be discarded.
[[nodiscard]] bool RequiresVideoReferenceReset(const VideoFrame& frame, std::optional<std::uint64_t> last_decoded_frame,
                                               const std::optional<VideoReferenceDependency>& dependency);

}  // namespace px
