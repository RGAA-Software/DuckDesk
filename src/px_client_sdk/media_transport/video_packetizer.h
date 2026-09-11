#pragma once

#include "nanors_codec.h"
#include <optional>

namespace px::media {
enum class VideoFrameKind : std::uint8_t { kPredicted = 1, kIdr = 2, kReferenceRecovery = 5 };
struct VideoPacketParameters final {
    std::uint32_t frame_index{1};
    std::uint32_t timestamp_90khz{};
    std::uint16_t sequence{};
    std::uint16_t datagram_size{1400};
    std::uint8_t fec_percent{20};
    std::uint8_t minimum_parity{2};
    VideoFrameKind kind{VideoFrameKind::kIdr};
};
struct PacketizedVideo final {
    std::vector<Packet> packets{};
    std::uint16_t next_sequence{};
    std::uint8_t block_count{};
    bool fec_disabled_for_large_frame{};
};

// Unencrypted upstream RTP/NV media payload. Pixels association/framing is outside this codec.
[[nodiscard]] std::optional<PacketizedVideo> PacketizeVideo(std::span<const std::uint8_t> encoded, VideoPacketParameters parameters);
} // namespace px::media
