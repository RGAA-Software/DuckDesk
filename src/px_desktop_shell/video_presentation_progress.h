#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace px::desktop {

// UI-thread confined. Count newly uploaded video only after the graphics API
// accepts presentation. Repainting the previous texture is not a video frame.
class VideoPresentationProgress final {
public:
    using Clock = std::chrono::steady_clock;
    struct Observation final {
        std::uint64_t generation{};
        std::int64_t gap_us{};
        std::int64_t upload_age_us{};
        std::uint64_t window_frames{};
        std::int64_t window_us{};
        std::int64_t maximum_gap_us{};
        std::int64_t maximum_upload_age_us{};
        std::int64_t maximum_render_us{};
    };

    void Uploaded(Clock::time_point now) {
        ++uploaded_generation_;
        uploaded_at_ = now;
    }

    [[nodiscard]] std::optional<Observation> Presented(bool accepted, Clock::time_point now, Clock::duration render_duration) {
        if (!accepted || uploaded_generation_ == presented_generation_) return std::nullopt;
        presented_generation_ = uploaded_generation_;
        const auto gap_us = previous_presented_at_ ? Microseconds(now - *previous_presented_at_) : 0;
        const auto upload_age_us = Microseconds(now - uploaded_at_);
        previous_presented_at_ = now;
        if (!window_started_at_) window_started_at_ = now;
        ++window_frames_;
        maximum_gap_us_ = std::max(maximum_gap_us_, gap_us);
        maximum_upload_age_us_ = std::max(maximum_upload_age_us_, upload_age_us);
        maximum_render_us_ = std::max(maximum_render_us_, Microseconds(render_duration));
        const auto window_us = Microseconds(now - *window_started_at_);
        Observation observation{.generation = presented_generation_, .gap_us = gap_us, .upload_age_us = upload_age_us};
        if (window_us >= 5'000'000) {
            observation.window_frames = window_frames_;
            observation.window_us = window_us;
            observation.maximum_gap_us = maximum_gap_us_;
            observation.maximum_upload_age_us = maximum_upload_age_us_;
            observation.maximum_render_us = maximum_render_us_;
            window_started_at_ = now;
            window_frames_ = 0;
            maximum_gap_us_ = 0;
            maximum_upload_age_us_ = 0;
            maximum_render_us_ = 0;
        }
        return observation;
    }

private:
    static std::int64_t Microseconds(Clock::duration duration) { return std::chrono::duration_cast<std::chrono::microseconds>(duration).count(); }
    std::uint64_t uploaded_generation_{};
    std::uint64_t presented_generation_{};
    Clock::time_point uploaded_at_{};
    std::optional<Clock::time_point> previous_presented_at_{};
    std::optional<Clock::time_point> window_started_at_{};
    std::uint64_t window_frames_{};
    std::int64_t maximum_gap_us_{};
    std::int64_t maximum_upload_age_us_{};
    std::int64_t maximum_render_us_{};
};

void LogVideoPresentation(const std::optional<VideoPresentationProgress::Observation>& observation, std::string_view backend);

}  // namespace px::desktop
