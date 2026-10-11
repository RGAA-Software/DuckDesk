#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace px::transport {

// Per-connection media budget supplied by the application configuration.
// Reserve room for audio/control, FEC and conservative wire overhead.
struct VideoSendBudget final {
    static constexpr std::uint64_t kDefaultTotalBitsPerSecond{10'000'000};
    static constexpr std::size_t kPacketsPerBurst{4};
    static constexpr std::size_t kPacketWireOverhead{128};

    explicit VideoSendBudget(std::uint64_t total_bps = kDefaultTotalBitsPerSecond)
        : total_bps_(std::clamp<std::uint64_t>(total_bps, 128'000, 200'000'000)) {}
    [[nodiscard]] std::uint64_t TotalBitsPerSecond() const { return total_bps_; }
    [[nodiscard]] std::uint64_t VideoWireBitsPerSecond() const { return total_bps_ * 96 / 100; }
    [[nodiscard]] std::uint64_t EncoderBitsPerSecond() const { return total_bps_ * 65 / 100; }

private:
    std::uint64_t total_bps_{};
};

// Shared across consecutive video batches on their serialized send executor.
// QUIC still controls congestion. This bounds application admission bursts;
// queued QUIC traffic can still be released in a burst after transport blockage.
class VideoSendPacing final {
public:
    using Clock = std::chrono::steady_clock;

    explicit VideoSendPacing(VideoSendBudget budget = VideoSendBudget{}) : budget_(budget) {}

    [[nodiscard]] Clock::time_point NextSendTime() const { return next_send_; }

    void Sent(std::uint64_t wire_bytes, Clock::time_point started) {
        const auto duration =
            std::chrono::nanoseconds{(wire_bytes * 8'000'000'000ULL + budget_.VideoWireBitsPerSecond() - 1) / budget_.VideoWireBitsPerSecond()};
        // Retain the clock across frame boundaries. A late executor may recover
        // at most one burst of credit, never all the time spent blocked or idle.
        next_send_ = std::max(next_send_ + duration, started);
    }

private:
    VideoSendBudget budget_{};
    Clock::time_point next_send_{};
};
}  // namespace px::transport
