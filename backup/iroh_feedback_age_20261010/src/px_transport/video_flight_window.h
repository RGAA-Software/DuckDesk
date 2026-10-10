#pragma once

#include "video_receive_feedback.h"

namespace px::transport {

// Caller holds the media sender mutex. Local send completion does not release credits.
// Complete remote reception (including frames awaiting reference repair) releases credits.
class VideoFlightWindow final {
public:
    using Clock = std::chrono::steady_clock;

    struct Snapshot final {
        std::size_t frames{};
        std::size_t bytes{};
        std::size_t budget_bytes{};
        std::uint64_t oldest_frame{};
        std::uint64_t newest_frame{};
        std::int64_t oldest_age_us{};
        std::int64_t progress_age_us{};
        std::int64_t probe_wait_us{};
        std::uint64_t forward_delay_growth_us{};
    };

    [[nodiscard]] Snapshot Inspect(std::uint8_t stream, Clock::time_point now) const {
        const auto position = streams_.find(stream);
        if (position == streams_.end() || position->second.pending.empty()) return {};
        const auto& window = position->second;
        return {.frames = window.pending.size(),
                .bytes = window.bytes,
                .budget_bytes = window.budget_bytes,
                .oldest_frame = window.pending.front().frame_index,
                .newest_frame = window.pending.back().frame_index,
                .oldest_age_us = std::chrono::duration_cast<std::chrono::microseconds>(now - window.pending.front().sent).count(),
                .progress_age_us = std::chrono::duration_cast<std::chrono::microseconds>(now - window.progress_at).count(),
                .probe_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(window.next_probe - now).count(),
                .forward_delay_growth_us = window.forward_delay_growth_us};
    }

    void ObserveRtt(std::uint64_t rtt_us) {
        if (rtt_us != 0) baseline_rtt_us_ = baseline_rtt_us_ ? std::min(*baseline_rtt_us_, rtt_us) : rtt_us;
    }

    [[nodiscard]] bool CanSend(std::uint8_t stream, std::size_t bytes, Clock::time_point now) const {
        const auto position = streams_.find(stream);
        if (position == streams_.end() || position->second.pending.empty()) return true;
        const auto& window = position->second;
        const auto age_limit = std::chrono::microseconds(200000 + std::min<std::uint64_t>(baseline_rtt_us_.value_or(0), 500000) * 2);
        // Acknowledged progress frees capacity immediately. An old outstanding frame
        // is not a stalled receiver while newer cumulative confirmations keep advancing.
        // QUIC already controls wire congestion. A low fixed frame count also
        // pauses tiny frames on feedback jitter, despite free byte capacity.
        // Keep history bounded independently of the byte and stalled-peer limits.
        if (window.pending.size() < 64 && window.bytes + bytes < window.budget_bytes && now - window.progress_at < age_limit) return true;
        // All outstanding frames may have lost a shard, so complete-frame credit alone can deadlock.
        // Permit a sparse recovery probe, without resetting the old debt or flushing QUIC/TCP.
        return now >= window.next_probe;
    }

    void Sent(std::uint8_t stream, std::uint64_t frame_index, std::size_t bytes, Clock::time_point now) {
        auto& window = streams_[stream];
        if (window.pending.empty()) window.progress_at = now;
        window.pending.push_back({frame_index, bytes, now});
        window.bytes += bytes;
        window.latest_frame_bytes = bytes;
        window.next_probe = now + std::chrono::milliseconds(250);
        // The transport closes a dead peer independently. Bound history even while sparse probes continue.
        if (window.pending.size() > 64) {
            window.bytes -= window.pending.front().bytes;
            window.pending.pop_front();
        }
    }

    void Observe(const VideoReceiveFeedback& report, Clock::time_point now) {
        const auto position = streams_.find(report.stream);
        if (position == streams_.end()) return;
        auto& window = position->second;
        if (report.elapsed_us <= window.feedback_elapsed_us || report.complete_frames < window.complete_frames) return;
        window.feedback_elapsed_us = report.elapsed_us;
        window.complete_frames = report.complete_frames;
        if (!report.latest_frame_index) return;
        const auto acknowledged = std::find_if(window.pending.begin(), window.pending.end(),
                                               [&report](const Pending& pending) { return pending.frame_index == *report.latest_frame_index; });
        if (acknowledged == window.pending.end()) return;
        // Compare receiver report-generation intervals with the corresponding
        // local frame-send intervals. Clock offsets cancel; the return trip
        // is excluded. Using `now - sent` here mistakes delayed feedback for
        // forward video congestion and unnecessarily pauses healthy capture.
        if (!window.baseline_report_elapsed_us) {
            window.baseline_sent = acknowledged->sent;
            window.baseline_report_elapsed_us = report.elapsed_us;
        }
        const auto source_interval_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(acknowledged->sent - window.baseline_sent).count());
        const auto receiver_interval_us = report.elapsed_us - *window.baseline_report_elapsed_us;
        if (receiver_interval_us < source_interval_us) {
            window.baseline_sent = acknowledged->sent;
            window.baseline_report_elapsed_us = report.elapsed_us;
            window.forward_delay_growth_us = 0;
        } else {
            window.forward_delay_growth_us = receiver_interval_us - source_interval_us;
        }
        if (window.forward_delay_growth_us > 100000) {
            window.constrained_until = now + std::chrono::seconds(1);
        }
        window.progress_at = now;
        const auto end = std::next(acknowledged);
        for (auto pending = window.pending.begin(); pending != end; ++pending) {
            window.bytes -= pending->bytes;
            window.confirmed_wire_bytes += pending->bytes;
        }
        window.pending.erase(window.pending.begin(), end);
        const auto sample_us = report.elapsed_us - window.budget_sample_elapsed_us;
        if (sample_us >= 250000) {
            // Bound latency using actual receipt throughput, including packet/FEC
            // overhead, instead of retaining 256 KiB even on a 2 Mbps path.
            // Include baseline RTT and two feedback periods; four recent frames
            // leave room for feedback granularity and scene-size changes.
            const auto horizon_us = 100000 + std::min<std::uint64_t>(baseline_rtt_us_.value_or(0), 500000);
            const auto throughput_budget = window.confirmed_wire_bytes * horizon_us / sample_us;
            const auto frame_budget = window.latest_frame_bytes * 4;
            // Low throughput can simply mean a static scene. Constrain only
            // after forward delay grows, then restore normal burst capacity
            // after one healthy second instead of trapping a recovered link.
            window.budget_bytes = now < window.constrained_until
                                      ? std::clamp<std::size_t>(std::max(throughput_budget, frame_budget), 32 * 1024, 256 * 1024)
                                      : 256 * 1024;
            window.confirmed_wire_bytes = 0;
            window.budget_sample_elapsed_us = report.elapsed_us;
        }
    }

    [[nodiscard]] std::size_t PendingBytes() const {
        std::size_t bytes{};
        for (const auto& [stream, window] : streams_) bytes += window.bytes;
        return bytes;
    }

private:
    struct Pending final {
        std::uint64_t frame_index{};
        std::size_t bytes{};
        Clock::time_point sent{};
    };
    struct Window final {
        std::deque<Pending> pending{};
        std::size_t bytes{};
        Clock::time_point next_probe{};
        Clock::time_point progress_at{};
        std::uint64_t feedback_elapsed_us{};
        std::uint64_t complete_frames{};
        std::size_t budget_bytes{256 * 1024};
        std::size_t latest_frame_bytes{};
        std::uint64_t confirmed_wire_bytes{};
        std::uint64_t budget_sample_elapsed_us{};
        Clock::time_point baseline_sent{};
        std::optional<std::uint64_t> baseline_report_elapsed_us{};
        std::uint64_t forward_delay_growth_us{};
        Clock::time_point constrained_until{};
    };
    std::map<std::uint8_t, Window> streams_{};
    std::optional<std::uint64_t> baseline_rtt_us_{};
};
}  // namespace px::transport
