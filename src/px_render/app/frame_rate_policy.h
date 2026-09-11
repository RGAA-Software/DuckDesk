#pragma once

#include <chrono>
#include <optional>

namespace px::render {

constexpr bool ValidFrameRate(int fps) noexcept {
    return fps >= 15 && fps <= 120;
}
constexpr int InitialFrameRate(int fps) noexcept {
    return ValidFrameRate(fps) ? fps : 60;
}

// Encoder-worker-owned admission clock. Fast Hook input must not exceed the rate declared to the encoder.
// Preserve the time anchor under normal jitter; after a stall accept one current frame, never a catch-up burst.
class FrameRateAdmission final {
  public:
    using Clock = std::chrono::steady_clock;

    bool Admit(Clock::time_point now, int fps) {
        if (!ValidFrameRate(fps))
            return false;
        const auto interval = std::chrono::nanoseconds{1'000'000'000 / fps};
        if (!next_ || fps_ != fps || now > *next_ + interval) {
            fps_ = fps;
            next_ = now + interval;
            return true;
        }
        // A small early allowance avoids dividing 60 FPS by two due to sub-millisecond capture jitter.
        if (now + interval / 4 < *next_)
            return false;
        *next_ += interval;
        return true;
    }

  private:
    int fps_{};
    std::optional<Clock::time_point> next_{};
};

} // namespace px::render
