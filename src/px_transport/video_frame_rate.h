#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace px::transport {

// Shed capture frames before encoding, after quality reduction has failed to clear sustained pressure.
// This preserves the encoded reference chain; it does not drop arbitrary encoded P frames.
class VideoFrameRate final {
public:
    using Clock = std::chrono::steady_clock;
    void Observe(std::uint64_t video_bps, bool pressure, Clock::time_point now) {
        if (pressure && video_bps <= 500000) {
            healthy_since_.reset();
            if (!pressure_since_) pressure_since_ = now;
            if (now - *pressure_since_ >= std::chrono::seconds(1) && now >= next_change_) {
                percent_ = std::max(25, percent_ / 2);
                next_change_ = now + std::chrono::seconds(2);
            }
        } else {
            pressure_since_.reset();
            if (pressure) {
                healthy_since_.reset();
                return;
            }
            if (!healthy_since_) healthy_since_ = now;
            if (now - *healthy_since_ >= std::chrono::seconds(3) && now >= next_change_) {
                percent_ = std::min(100, percent_ + 25);
                next_change_ = now + std::chrono::seconds(2);
            }
        }
    }

    [[nodiscard]] int Limit(int requested) const {
        const auto configured = std::max(1, requested);
        return std::min(configured, std::max(15, configured * percent_ / 100));
    }

private:
    int percent_{100};
    std::optional<Clock::time_point> pressure_since_{};
    std::optional<Clock::time_point> healthy_since_{};
    Clock::time_point next_change_{};
};
}  // namespace px::transport
