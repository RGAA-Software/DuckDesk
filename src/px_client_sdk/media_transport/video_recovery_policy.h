#pragma once

#include <limits>

#include "video_stream.h"

namespace px::media {

enum class VideoRecoveryRequestKind { kInvalidateReferences, kKeyFrame };

struct VideoRecoveryRequest final {
    VideoRecoveryRequestKind kind{VideoRecoveryRequestKind::kKeyFrame};
    std::uint8_t stream{};
    std::string monitor{};
    std::optional<std::uint64_t> invalid_reference_frame{};
    std::uint64_t recovery_age_us{};
};

// Confined to the receive executor, including its retry timer. A complete frame cancels speculative loss recovery.
class VideoRecoveryPolicy final {
public:
    static constexpr std::uint64_t kRetryIntervalUs{100000};
    static constexpr std::uint64_t kKeyFrameDeadlineUs{500000};
    static constexpr std::uint64_t kKeyFrameRetryUs{1000000};
    static constexpr std::uint64_t kStalledRetryUs{4000000};
    static constexpr std::uint64_t kIncompleteFrameDeadlineUs{100000};

    // Relay's outer reliable carrier can pause a partial frame without losing
    // its reference. Keep actual loss recovery and the sparse watchdog active.
    void SetPartialIdleRepairEnabled(bool enabled) { partial_idle_repair_enabled_ = enabled; }

    [[nodiscard]] std::optional<VideoRecoveryRequest> Observe(std::uint8_t stream_index, const VideoStreamOutput& received, std::uint64_t now_us) {
        auto& recovery = streams_[stream_index];
        if (received.completed_frame_index) {
            recovery.monitor = received.completed_monitor;
            recovery.progress_since_request = true;
        }
        if (received.frame) {
            recovery.monitor = received.frame->monitor;
            recovery.last_reference_frame = received.frame->frame_index;
            recovery.started_us.reset();
            recovery.last_partial_progress_us.reset();
            recovery.last_request_us.reset();
            recovery.last_key_frame_request_us.reset();
            recovery.invalid_reference_frame.reset();
            recovery.requires_key_frame = false;
            recovery.progress_since_request = false;
            return std::nullopt;
        }
        if (received.incomplete_frame) {
            if (!recovery.started_us) recovery.started_us = now_us;
            recovery.last_partial_progress_us = now_us;
        }
        if (received.needs_idr || received.invalid_reference_frame) {
            if (!recovery.started_us) recovery.started_us = now_us;
            recovery.requires_key_frame |= received.needs_idr;
            if (received.invalid_reference_frame &&
                (!recovery.invalid_reference_frame || *received.invalid_reference_frame < *recovery.invalid_reference_frame)) {
                recovery.invalid_reference_frame = received.invalid_reference_frame;
            }
        }
        return Poll(stream_index, recovery, now_us);
    }

    [[nodiscard]] std::vector<VideoRecoveryRequest> PollDue(std::uint64_t now_us) {
        std::vector<VideoRecoveryRequest> requests{};
        for (auto& [stream_index, recovery] : streams_) {
            if (auto request = Poll(stream_index, recovery, now_us)) requests.push_back(std::move(*request));
        }
        return requests;
    }

    void Reset() { streams_.clear(); }

private:
    struct Recovery final {
        std::string monitor{};
        std::optional<std::uint64_t> started_us{};
        std::optional<std::uint64_t> last_partial_progress_us{};
        std::optional<std::uint64_t> last_request_us{};
        std::optional<std::uint64_t> last_key_frame_request_us{};
        std::optional<std::uint64_t> invalid_reference_frame{};
        std::optional<std::uint64_t> last_reference_frame{};
        bool requires_key_frame{};
        bool progress_since_request{};
    };

    [[nodiscard]] std::optional<VideoRecoveryRequest> Poll(std::uint8_t stream_index, Recovery& recovery, std::uint64_t now_us) const {
        if (!recovery.started_us || now_us < *recovery.started_us) return std::nullopt;
        // Capture can stop when complete-frame credits fill. Waiting for a
        // subsequent complete frame would then postpone RFI until a sparse
        // sender probe. Allow FEC/reordering first, then request one small
        // repair using the last established reference. No-progress retries
        // remain sparse, and an unknown initial reference keeps its watchdog.
        if (partial_idle_repair_enabled_ && !recovery.requires_key_frame && !recovery.invalid_reference_frame && recovery.last_reference_frame &&
            recovery.last_partial_progress_us && now_us >= *recovery.last_partial_progress_us &&
            now_us - *recovery.last_partial_progress_us >= kIncompleteFrameDeadlineUs) {
            if (*recovery.last_reference_frame == std::numeric_limits<std::uint64_t>::max())
                recovery.requires_key_frame = true;
            else
                recovery.invalid_reference_frame = *recovery.last_reference_frame + 1;
            // A large frame may have arrived normally for hundreds of ms.
            // Escalation measures the stall, not that healthy assembly time.
            recovery.started_us = recovery.last_partial_progress_us;
        }
        const auto recovery_age_us = now_us - *recovery.started_us;
        if (!recovery.requires_key_frame && !recovery.invalid_reference_frame && recovery_age_us < kStalledRetryUs) return std::nullopt;
        // With no complete-frame progress, repeated large IDRs only deepen a congested queue.
        // A sparse watchdog still recovers a lost final frame or a static source.
        if (recovery.last_request_us && !recovery.progress_since_request && now_us - *recovery.last_request_us < kStalledRetryUs) return std::nullopt;
        const bool request_key_frame = recovery.requires_key_frame || recovery_age_us >= kKeyFrameDeadlineUs;
        const auto last_request_us = request_key_frame ? recovery.last_key_frame_request_us : recovery.last_request_us;
        const auto retry_interval_us = request_key_frame ? kKeyFrameRetryUs : kRetryIntervalUs;
        if (last_request_us && (now_us < *last_request_us || now_us - *last_request_us < retry_interval_us)) return std::nullopt;
        recovery.last_request_us = now_us;
        recovery.progress_since_request = false;
        if (request_key_frame) recovery.last_key_frame_request_us = now_us;
        return VideoRecoveryRequest{.kind = request_key_frame ? VideoRecoveryRequestKind::kKeyFrame : VideoRecoveryRequestKind::kInvalidateReferences,
                                    .stream = stream_index,
                                    .monitor = recovery.monitor,
                                    .invalid_reference_frame = recovery.invalid_reference_frame,
                                    .recovery_age_us = recovery_age_us};
    }

    std::map<std::uint8_t, Recovery> streams_{};
    bool partial_idle_repair_enabled_{true};
};

}  // namespace px::media
