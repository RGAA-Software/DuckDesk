#pragma once

#include <algorithm>
#include <chrono>
#include <optional>

#include "transport.h"

namespace px::transport {

// Encoder feedback, not a second packet congestion controller. QUIC still owns wire pacing.
// A growing RTT is a queue signal even when the Relay's outer TCP hides packet loss.
class VideoRateControl final {
public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] std::uint64_t Update(std::uint64_t ceiling_bps, const ConnectionSnapshot& snapshot, Clock::time_point now,
                                       bool media_backpressure = false, bool receiver_stalled = false, std::uint64_t received_bps = 0) {
        if (!rate_bps_) rate_bps_ = ceiling_bps;
        rate_bps_ = std::min(*rate_bps_, ceiling_bps);
        if (last_sample_ && now - *last_sample_ < std::chrono::milliseconds(500)) return *rate_bps_;
        last_sample_ = now;
        if ((snapshot.path == PathKind::kUnknown || snapshot.rtt_us == 0) && !receiver_stalled && !media_backpressure) return *rate_bps_;
        if (snapshot.rtt_us != 0 && (path_ != snapshot.path || !baseline_rtt_us_)) {
            path_ = snapshot.path;
            baseline_rtt_us_ = snapshot.rtt_us;
            stable_since_ = now;
            pressure_since_.reset();
            if (!receiver_stalled && !media_backpressure) return *rate_bps_;
        }
        if (snapshot.rtt_us != 0 && baseline_rtt_us_) baseline_rtt_us_ = std::min(*baseline_rtt_us_, snapshot.rtt_us);
        const auto queue_delay_us = snapshot.rtt_us > baseline_rtt_us_.value_or(0) ? snapshot.rtt_us - baseline_rtt_us_.value_or(0) : 0;
        const auto congested_threshold_us = std::max<std::uint64_t>(80000, baseline_rtt_us_.value_or(0));
        if (receiver_stalled || media_backpressure || queue_delay_us > congested_threshold_us) {
            if (!pressure_since_) pressure_since_ = now;
            stable_since_ = now;
            // The flight window already bounds queued video. A single 50–150 ms
            // scheduling/RTT spike must not repeatedly halve quality. Missing
            // reception remains urgent; sustained large queues get a faster response.
            const bool large_queue = queue_delay_us > 250000;
            const auto required_pressure = large_queue ? std::chrono::milliseconds(500) : std::chrono::milliseconds(1000);
            if (!receiver_stalled && (now - *pressure_since_ < required_pressure || now < next_reduction_)) return *rate_bps_;
            const auto floor_bps = std::min<std::uint64_t>(ceiling_bps, 250'000);
            const bool severe = receiver_stalled || large_queue;
            rate_bps_ = std::max(floor_bps, *rate_bps_ * (severe ? 50 : 80) / 100);
            if (receiver_stalled && received_bps > 0) rate_bps_ = std::max(floor_bps, std::min(*rate_bps_, received_bps * 80 / 100));
            next_reduction_ = now + (severe ? std::chrono::milliseconds(500) : std::chrono::milliseconds(1000));
        } else if (queue_delay_us > congested_threshold_us / 2) {
            pressure_since_.reset();
            stable_since_ = now;
        } else {
            pressure_since_.reset();
            if (now - stable_since_ >= std::chrono::seconds(2)) {
                // Spaced probes restore quality promptly without forcing a rate
                // change on every feedback report or jumping straight to the ceiling.
                rate_bps_ = std::min(ceiling_bps, *rate_bps_ + std::max<std::uint64_t>(250000, *rate_bps_ / 4));
                stable_since_ = now;
            }
        }
        return *rate_bps_;
    }

private:
    std::optional<std::uint64_t> rate_bps_{};
    std::optional<std::uint64_t> baseline_rtt_us_{};
    std::optional<Clock::time_point> last_sample_{};
    std::optional<Clock::time_point> pressure_since_{};
    Clock::time_point next_reduction_{};
    Clock::time_point stable_since_{};
    PathKind path_{PathKind::kUnknown};
};
}  // namespace px::transport
