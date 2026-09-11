#pragma once

#include "video_packetizer.h"
#include <optional>

namespace px::media {
struct VideoReceiveStatistics final {
    std::uint64_t data_packets{};
    std::uint64_t parity_packets{};
    std::uint64_t duplicates{};
    std::uint64_t late_packets{};
    std::uint64_t reordered_packets{};
    std::uint64_t recovered_data{};
    std::uint64_t completed_frames{};
    std::uint64_t predicted_losses{};
    std::uint64_t prediction_corrections{};
    std::uint64_t final_loss_events{};
    std::uint64_t unrecoverable_frames{};
    std::uint64_t malformed_packets{};
};
struct VideoLoss final {
    std::uint32_t frame_index{};
    bool speculative{};
};
struct VideoQueueOutput final {
    std::vector<Packet> data_packets{};
    std::vector<VideoLoss> losses{};
    std::size_t recovered_data{};
    bool rejected{};
    bool malformed{};
};

// One instance per media stream. Synchronous values only; no callback or socket ownership.
class VideoReceiveQueue final {
  public:
    explicit VideoReceiveQueue(std::uint16_t datagram_size);
    [[nodiscard]] VideoQueueOutput Feed(std::span<const std::uint8_t> packet, std::uint64_t presentation_us);
    void Reset();
    [[nodiscard]] const VideoReceiveStatistics& Statistics() const {
        return statistics_;
    }

  private:
    struct Block final {
        std::uint16_t base_sequence{};
        std::uint16_t data_count{};
        std::uint16_t parity_count{};
        std::uint8_t percentage{};
        std::uint8_t index{};
        std::uint8_t last_index{};
        std::size_t received{};
        std::size_t data_received{};
        std::size_t highest_index{};
        std::size_t missing_before_highest{};
        std::vector<Packet> shards{};
        std::vector<std::uint8_t> missing{};
    };
    void Lose(VideoQueueOutput& output, std::uint32_t frame, bool speculative);
    std::uint16_t datagram_size_{};
    std::uint32_t frame_index_{1};
    std::uint8_t next_block_{};
    bool reported_loss_{};
    bool seen_reordering_{};
    std::uint64_t last_reordered_us_{};
    std::optional<Block> block_{};
    std::vector<Packet> staged_{};
    VideoReceiveStatistics statistics_{};
    bool observed_frame_{};
};
} // namespace px::media
