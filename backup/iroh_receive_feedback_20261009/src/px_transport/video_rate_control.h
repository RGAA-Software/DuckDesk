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
                                       bool media_backpressure = false) {
        if (!rate_bps_) rate_bps_ = ceiling_bps;
        rate_bps_ = std::min(*rate_bps_, ceiling_bps);
        if (last_sample_ && now - *last_sample_ < std::chrono::milliseconds(500)) return *rate_bps_;
        last_sample_ = now;
        if (snapshot.path == PathKind::kUnknown || snapshot.rtt_us == 0) return *rate_bps_;
        if (path_ != snapshot.path || !baseline_rtt_us_) {
            path_ = snapshot.path;
            baseline_rtt_us_ = snapshot.rtt_us;
            stable_since_ = now;
            return *rate_bps_;
        }
        baseline_rtt_us_ = std::min(*baseline_rtt_us_, snapshot.rtt_us);
        const auto queue_delay_us = snapshot.rtt_us - *baseline_rtt_us_;
        const auto congested_threshold_us = std::max<std::uint64_t>(40000, *baseline_rtt_us_);
        if (media_backpressure || queue_delay_us > congested_threshold_us) {
            // Small frames still carry fixed packet padding, FEC and audio. A 1 Mbps
            // encoder floor can keep a 2 Mbps Relay path congested during recovery.
            const auto floor_bps = std::min<std::uint64_t>(ceiling_bps, 250'000);
            rate_bps_ = std::max(floor_bps, *rate_bps_ * (media_backpressure || queue_delay_us > 200000 ? 50 : 80) / 100);
            stable_since_ = now;
        } else if (queue_delay_us > congested_threshold_us / 2) {
            stable_since_ = now;
        } else if (now - stable_since_ >= std::chrono::seconds(5)) {
            // Native encoders may reopen on a rate change. Recover in spaced steps,
            // rather than forcing a new encoder/key frame every sampling interval.
            rate_bps_ = std::min(ceiling_bps, *rate_bps_ + std::max<std::uint64_t>(100000, *rate_bps_ / 5));
            stable_since_ = now;
        }
        return *rate_bps_;
    }

private:
    std::optional<std::uint64_t> rate_bps_{};
    std::optional<std::uint64_t> baseline_rtt_us_{};
    std::optional<Clock::time_point> last_sample_{};
    Clock::time_point stable_since_{};
    PathKind path_{PathKind::kUnknown};
};
}  // namespace px::transport
