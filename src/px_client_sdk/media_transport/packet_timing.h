#pragma once

#include "media_wire.h"
#include <array>
#include <chrono>

namespace px::media {
// Diagnostics only: wire identity, never payload, credentials or a recovery decision.
struct VideoPacketIdentity final {
    std::uint32_t frame{};
    std::uint16_t sequence{};
    std::uint16_t shard{};
    std::uint8_t stream{};
    std::uint8_t block{};
    bool operator==(const VideoPacketIdentity&) const = default;
};

inline std::uint64_t MediaSteadyMicros() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline std::optional<VideoPacketIdentity> InspectVideoPacket(const MediaDatagram& datagram) {
    if (datagram.kind != MediaKind::kVideo || datagram.payload.size() < 32)
        return {};
    const auto little = [](std::span<const std::uint8_t> bytes, std::size_t offset) {
        std::uint32_t value{};
        for (std::size_t index{}; index < 4; ++index)
            value |= static_cast<std::uint32_t>(bytes[offset + index]) << (8 * index);
        return value;
    };
    return VideoPacketIdentity{little(datagram.payload, 20), static_cast<std::uint16_t>(wire::Get(datagram.payload, 2, 2)),
                               static_cast<std::uint16_t>((little(datagram.payload, 28) >> 12) & 1023), datagram.stream,
                               static_cast<std::uint8_t>((datagram.payload[27] >> 4) & 3)};
}

struct VideoPacketGap final {
    VideoPacketIdentity previous{};
    VideoPacketIdentity current{};
    std::uint64_t previous_us{};
    std::uint64_t current_us{};
};

// Owned by one socket executor. Fixed memory; reconnect resets history. No asynchronous work or retained borrowed views.
class VideoPacketTiming final {
  public:
    std::optional<VideoPacketGap> Observe(const VideoPacketIdentity& packet, std::uint64_t now_us, std::uint64_t threshold_us = 50000) {
        auto& previous = previous_[packet.stream];
        std::optional<VideoPacketGap> gap{};
        if (previous && now_us >= previous->time_us && now_us - previous->time_us > threshold_us)
            gap = VideoPacketGap{previous->identity, packet, previous->time_us, now_us};
        previous = Observation{packet, now_us};
        return gap;
    }

    void Reset() {
        previous_ = {};
    }

  private:
    struct Observation final {
        VideoPacketIdentity identity{};
        std::uint64_t time_us{};
    };
    std::array<std::optional<Observation>, 256> previous_{};
};
} // namespace px::media
