#pragma once

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

    [[nodiscard]] std::optional<VideoRecoveryRequest> Observe(std::uint8_t stream_index, const VideoStreamOutput& received, std::uint64_t now_us) {
        auto& recovery = streams_[stream_index];
        if (received.frame) {
            recovery.monitor = received.frame->monitor;
            recovery.started_us.reset();
            recovery.last_request_us.reset();
            recovery.invalid_reference_frame.reset();
            recovery.requires_key_frame = false;
            return std::nullopt;
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
        std::optional<std::uint64_t> last_request_us{};
        std::optional<std::uint64_t> last_key_frame_request_us{};
        std::optional<std::uint64_t> invalid_reference_frame{};
        bool requires_key_frame{};
    };

    [[nodiscard]] static std::optional<VideoRecoveryRequest> Poll(std::uint8_t stream_index, Recovery& recovery, std::uint64_t now_us) {
        if (!recovery.started_us || now_us < *recovery.started_us) return std::nullopt;
        const auto recovery_age_us = now_us - *recovery.started_us;
        const bool request_key_frame = recovery.requires_key_frame || recovery_age_us >= kKeyFrameDeadlineUs;
        const auto last_request_us = request_key_frame ? recovery.last_key_frame_request_us : recovery.last_request_us;
        const auto retry_interval_us = request_key_frame ? kKeyFrameRetryUs : kRetryIntervalUs;
        if (last_request_us && (now_us < *last_request_us || now_us - *last_request_us < retry_interval_us)) return std::nullopt;
        recovery.last_request_us = now_us;
        if (request_key_frame) recovery.last_key_frame_request_us = now_us;
        return VideoRecoveryRequest{.kind = request_key_frame ? VideoRecoveryRequestKind::kKeyFrame : VideoRecoveryRequestKind::kInvalidateReferences,
                                    .stream = stream_index,
                                    .monitor = recovery.monitor,
                                    .invalid_reference_frame = recovery.invalid_reference_frame,
                                    .recovery_age_us = recovery_age_us};
    }

    std::map<std::uint8_t, Recovery> streams_{};
};

}  // namespace px::media
