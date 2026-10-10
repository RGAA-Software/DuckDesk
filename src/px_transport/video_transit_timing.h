#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace px::transport {

// RTP carries the sender's monotonic clock, not a wall clock. Compare elapsed
// intervals to remove clock offset; the minimum observed transit is the baseline.
// This diagnoses additional delay, not absolute one-way latency. Receiver confined.
class VideoTransitTiming final {
public:
    struct Sample final {
        std::int64_t source_gap_us{};
        std::int64_t receive_gap_us{};
        std::int64_t excess_transit_us{};
    };

    [[nodiscard]] Sample Observe(std::uint32_t timestamp_90khz, std::uint64_t received_us) {
        if (!previous_timestamp_ || received_us < previous_received_us_) {
            previous_timestamp_ = timestamp_90khz;
            previous_received_us_ = received_us;
            transit_ticks_ = minimum_transit_ticks_ = 0;
            return {};
        }
        // Unsigned subtraction unwraps RTP's 32-bit clock across its wrap boundary.
        const auto source_ticks = static_cast<std::uint32_t>(timestamp_90khz - *previous_timestamp_);
        const auto receive_gap_us = static_cast<std::int64_t>(received_us - previous_received_us_);
        previous_timestamp_ = timestamp_90khz;
        previous_received_us_ = received_us;
        if (source_ticks > 0x7fffffffU) {
            transit_ticks_ = minimum_transit_ticks_ = 0;
            return {};
        }
        // Nine ticks per microsecond on a shared 9 MHz integer scale; avoid drift
        // from rounding each 90 kHz source interval to whole microseconds.
        transit_ticks_ += receive_gap_us * 9 - static_cast<std::int64_t>(source_ticks) * 100;
        minimum_transit_ticks_ = std::min(minimum_transit_ticks_, transit_ticks_);
        return {.source_gap_us = static_cast<std::int64_t>(source_ticks) * 100 / 9,
                .receive_gap_us = receive_gap_us,
                .excess_transit_us = (transit_ticks_ - minimum_transit_ticks_) / 9};
    }

private:
    std::optional<std::uint32_t> previous_timestamp_{};
    std::uint64_t previous_received_us_{};
    std::int64_t transit_ticks_{};
    std::int64_t minimum_transit_ticks_{};
};
}  // namespace px::transport
