#pragma once

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <optional>

#include "media_transport/video_stream.h"

namespace px::transport {

struct VideoReceiveFeedback final {
    std::uint8_t stream{};
    std::uint64_t elapsed_us{};
    std::uint64_t complete_frames{};
    std::uint64_t complete_bytes{};
    std::optional<std::uint64_t> latest_frame_index{};
};

// Receive-worker confined. Keep reporting a stalled stream, including before its first complete frame.
class VideoReceiveProgress final {
public:
    void Observe(std::uint8_t stream, const media::VideoStreamOutput& received, std::uint64_t now_us) {
        auto [position, inserted] = streams_.try_emplace(stream, Window{.started_us = now_us, .reported_us = now_us});
        auto& window = position->second;
        if (received.completed_frame_index) {
            ++window.feedback.complete_frames;
            window.feedback.complete_bytes += received.completed_bytes;
            window.feedback.latest_frame_index = received.completed_frame_index;
        }
    }

    [[nodiscard]] std::vector<VideoReceiveFeedback> Poll(std::uint64_t now_us) {
        std::vector<VideoReceiveFeedback> reports{};
        for (auto& [stream, window] : streams_) {
            if (now_us < window.reported_us || now_us - window.reported_us < 250000) continue;
            window.feedback.stream = stream;
            window.feedback.elapsed_us = now_us - window.started_us;
            window.reported_us = now_us;
            reports.push_back(window.feedback);
        }
        return reports;
    }

private:
    struct Window final {
        std::uint64_t started_us{};
        std::uint64_t reported_us{};
        VideoReceiveFeedback feedback{};
    };
    std::map<std::uint8_t, Window> streams_{};
};

// Protected by the session rate mutex. Tracks actual complete reception, never local QUIC enqueue success.
class VideoDeliveryProgress final {
public:
    using Clock = std::chrono::steady_clock;

    void Offered(std::uint8_t stream, std::uint64_t frame_index, Clock::time_point now) {
        auto& window = streams_[stream];
        if (!window.pending_since) window.pending_since = now;
        window.pending.push_back({frame_index, now});
        if (window.pending.size() > 256) window.pending.pop_front();
    }

    bool Observe(const VideoReceiveFeedback& report, Clock::time_point now) {
        const auto position = streams_.find(report.stream);
        if (position == streams_.end()) return false;
        auto& window = position->second;
        if (report.elapsed_us <= window.previous.elapsed_us || report.complete_frames < window.previous.complete_frames ||
            report.complete_bytes < window.previous.complete_bytes)
            return false;
        const auto interval_us = report.elapsed_us - window.previous.elapsed_us;
        const auto received_bytes = report.complete_bytes - window.previous.complete_bytes;
        // Reject impossible counters before arithmetic; this is a feedback bound, not an authorization mechanism.
        if (received_bytes > 256ULL * 1024 * 1024 || interval_us < 100000) return false;
        window.received_bps = received_bytes * 8000000 / interval_us;
        window.previous = report;
        if (report.latest_frame_index) {
            const auto acknowledged = std::find_if(window.pending.begin(), window.pending.end(),
                                                   [&report](const Pending& pending) { return pending.frame_index == *report.latest_frame_index; });
            if (acknowledged != window.pending.end()) {
                const auto latency = now - acknowledged->offered;
                window.baseline_latency = window.baseline_latency ? std::min(*window.baseline_latency, latency) : latency;
                window.pending.erase(window.pending.begin(), std::next(acknowledged));
                window.pending_since = window.pending.empty() ? std::nullopt : std::optional{window.pending.front().offered};
            }
        }
        return true;
    }

    [[nodiscard]] bool Congested(Clock::time_point now) const {
        for (const auto& [stream, window] : streams_) {
            const auto threshold = std::max<Clock::duration>(std::chrono::milliseconds(500), window.baseline_latency.value_or(Clock::duration{}) * 3);
            if (window.pending_since && now - *window.pending_since > threshold) return true;
        }
        return false;
    }

    [[nodiscard]] std::uint64_t ReceivedBitrate() const {
        std::uint64_t total_bps{};
        for (const auto& [stream, window] : streams_) total_bps += window.received_bps;
        return total_bps;
    }

private:
    struct Pending final {
        std::uint64_t frame_index{};
        Clock::time_point offered{};
    };
    struct Window final {
        std::deque<Pending> pending{};
        std::optional<Clock::time_point> pending_since{};
        std::optional<Clock::duration> baseline_latency{};
        VideoReceiveFeedback previous{};
        std::uint64_t received_bps{};
    };
    std::map<std::uint8_t, Window> streams_{};
};
}  // namespace px::transport
