#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace px {

// Confined to a monitor's video queue. EAGAIN is normal for an individual input;
// continued accepted input without any image needs a fresh decoder reference.
class DecoderOutputProgress final {
public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] bool Observe(bool produced_image, Clock::time_point now) {
        if (produced_image) {
            Reset();
            return false;
        }
        if (!first_without_output_) first_without_output_ = now;
        ++accepted_without_output_;
        return accepted_without_output_ >= 4 && now - *first_without_output_ >= std::chrono::milliseconds{100};
    }

    void Reset() {
        first_without_output_.reset();
        accepted_without_output_ = 0;
    }

private:
    std::optional<Clock::time_point> first_without_output_{};
    std::uint64_t accepted_without_output_{};
};

}  // namespace px
