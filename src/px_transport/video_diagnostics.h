#pragma once

#include <array>
#include <bitset>
#include <map>
#include <optional>
#include <string>

#include "media_transport/media_wire.h"
#include "media_transport/packet_timing.h"
#include "media_transport/video_packetizer.h"

namespace px::transport {

// Temporary, payload-free diagnostics. Local monotonic clocks must never be subtracted across hosts.
struct VideoPacketIdentity final {
    std::uint32_t wire_frame{};
    std::uint32_t timestamp{};
    std::uint16_t sequence{};
    std::uint16_t shard{};
    std::uint16_t data_count{};
    std::uint16_t parity_count{};
    std::uint8_t block{};
    std::uint8_t last_block{};
};
[[nodiscard]] std::optional<VideoPacketIdentity> ReadVideoPacketIdentity(std::span<const std::uint8_t> packet);

class VideoReceiveDiagnostics final {
public:
    ~VideoReceiveDiagnostics();
    void Observe(const media::MediaDatagram& packet, std::uint64_t now_us);
    void Completed(std::uint8_t stream, std::uint32_t wire_frame, std::uint64_t encoder_frame, std::optional<media::VideoFrameKind> kind,
                   bool delivered);
    void Flush(std::uint64_t now_us, bool all = false);
    [[nodiscard]] bool Active(std::uint64_t now_us) const { return now_us >= started_us_ && now_us - started_us_ < 90'000'000; }
    [[nodiscard]] std::uint64_t Id() const { return started_us_; }
    struct Receipt final {
        std::uint64_t encoder_frame{};
        std::optional<media::VideoFrameKind> kind{};
        std::size_t unique_shards{};
        std::size_t duplicate_shards{};
        bool completed{};
        bool delivered{};
    };
    [[nodiscard]] std::optional<Receipt> Inspect(std::uint8_t stream, std::uint32_t wire_frame) const;

private:
    struct BlockReceipt final {
        std::bitset<1024> shards{};
        std::uint16_t data_count{};
        std::uint16_t parity_count{};
    };
    struct FrameTrace final {
        std::uint64_t first_us{};
        std::uint32_t timestamp{};
        std::size_t samples{};
        bool truncated{};
        std::string arrivals{};
        Receipt receipt{};
        std::array<BlockReceipt, 4> blocks{};
        std::uint8_t last_block{};
    };
    const std::uint64_t started_us_{media::MediaSteadyMicros()};
    std::map<std::pair<std::uint8_t, std::uint32_t>, FrameTrace> frames_{};
    std::uint64_t next_flush_us_{};
};
}  // namespace px::transport
