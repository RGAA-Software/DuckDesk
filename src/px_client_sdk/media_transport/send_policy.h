#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace px::media {

// Preserve fractional timer phase, but never bank more than one millisecond of late-send credit.
inline std::chrono::steady_clock::time_point NextBatchDeadline(std::chrono::steady_clock::time_point previous,
                                                               std::chrono::steady_clock::time_point actual_start,
                                                               std::chrono::nanoseconds batch_duration) {
    return std::max(previous, actual_start - std::chrono::milliseconds(1)) + batch_duration;
}

// Sunshine rtsp.cpp budget allocation, with stereo audio FEC included in the reservation.
// The application selection is a total Native media budget, not an uplink measurement.
struct SendBudget final {
    std::uint64_t total_bps{};
    std::uint64_t video_bps{};
    std::uint64_t video_wire_bps{};
    std::uint64_t audio_reserve_bps{};

    static SendBudget FromTotal(std::uint64_t requested_bps, unsigned fec_percent, std::uint16_t datagram_size = 1400) {
        SendBudget result{};
        result.total_bps = std::clamp<std::uint64_t>(requested_bps, 1'000'000, 1'000'000'000);
        // Current wire profile pads every audio shard. 50 data + 25 parity packets/s, not the Opus payload bitrate.
        const auto mtu = std::clamp<std::uint64_t>(datagram_size, 576, 1500);
        result.audio_reserve_bps = ((mtu + 36) * 50 + (mtu + 48) * 25) * 8;
        result.video_wire_bps = result.total_bps - result.audio_reserve_bps;
        auto video = result.video_wire_bps * (100 - std::min(fec_percent, 80U)) / 100;
        video -= std::min<std::uint64_t>(500'000, video / 10);
        result.video_bps = video;
        return result;
    }
};

// The encoder budget controls sustained network usage. Packet pacing has a different job: it only spaces short UDP bursts enough to avoid
// overflowing the socket/driver queue. Sunshine uses roughly 80% of a 1 Gbps link here rather than pacing every frame at the configured
// encoder bitrate. Using the encoder budget as the packet pacing rate makes a large IDR block the video sender for hundreds of milliseconds.
struct VideoPacketPacing final {
    static constexpr std::uint64_t kWireBitsPerSecond = 800'000'000;

    [[nodiscard]] static std::size_t PacketsPerMillisecond(const std::uint64_t wire_packet_bytes) {
        return std::max<std::size_t>(1, kWireBitsPerSecond / 8 / 1000 / std::max<std::uint64_t>(1, wire_packet_bytes));
    }

    [[nodiscard]] static std::chrono::nanoseconds Duration(const std::uint64_t wire_bytes) {
        return std::chrono::nanoseconds(wire_bytes * 8'000'000'000ULL / kWireBitsPerSecond);
    }
};

// Only actually encoded timestamps occupy the reference window; capture timestamps may skip.
// A queued request is not a recovery confirmation. The encoder confirms only after output.
class ReferenceRecovery final {
  public:
    void Request(std::uint64_t first) {
        pending_ = pending_ ? std::min(*pending_, first) : first;
    }
    [[nodiscard]] bool Pending() const {
        return pending_.has_value();
    }
    [[nodiscard]] std::optional<std::vector<std::uint64_t>> Take(std::size_t capacity) {
        if (!pending_)
            return std::vector<std::uint64_t>{};
        const auto first = *pending_;
        pending_.reset();
        const auto begin = std::lower_bound(history_.begin(), history_.end(), first);
        const auto count = static_cast<std::size_t>(std::distance(begin, history_.end()));
        if (capacity < 2 || begin == history_.begin() || begin == history_.end() || count >= capacity)
            return std::nullopt;
        return std::vector<std::uint64_t>(begin, history_.end());
    }
    void Output(std::uint64_t timestamp, bool idr) {
        if (idr || (!history_.empty() && timestamp <= history_.back()))
            history_.clear();
        history_.push_back(timestamp);
        if (history_.size() > 64)
            history_.pop_front();
    }
    void Reset() {
        history_.clear();
        pending_.reset();
    }

  private:
    std::deque<std::uint64_t> history_{};
    std::optional<std::uint64_t> pending_{};
};

} // namespace px::media
