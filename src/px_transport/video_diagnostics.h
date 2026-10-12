#pragma once

#include <array>
#include <bitset>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "media_transport/media_wire.h"
#include "media_transport/packet_timing.h"
#include "media_transport/video_packetizer.h"
#include "transport.h"

namespace px::transport {

// Caller serializes access. Keep metadata after the initial verbose window;
// no encoded bytes, packet payloads or asynchronous callbacks are retained.
class VideoSendDiagnostics final {
public:
    struct Submission final {
        std::uint64_t trace{};
        std::uint8_t stream{};
        std::uint32_t wire_frame{};
        std::uint64_t encoder_frame{};
        bool local_accepted{};
        std::uint64_t origin_us{};
        std::optional<std::uint64_t> first_accepted_us{};
        std::optional<std::uint64_t> last_accepted_us{};
        std::uint64_t queue_us{};
        std::uint64_t total_us{};
        std::uint64_t maximum_write_us{};
        std::size_t accepted_packets{};
        std::size_t bytes{};
        ConnectionSnapshot transport{};
    };
    void Record(Submission submission);
    [[nodiscard]] std::vector<Submission> RecentForGap(std::uint8_t stream, std::uint64_t now_us, std::uint64_t arrival_gap_us,
                                                       std::uint64_t generated_gap_us, std::uint64_t receipt_progress_gap_us);
    void Clear();

private:
    std::deque<Submission> submissions_{};
    std::optional<std::uint64_t> next_dump_us_{};
};

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
        std::optional<std::uint64_t> first_packet_received_us{};
        std::optional<std::uint64_t> latest_packet_received_us{};
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
