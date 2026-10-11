#pragma once

#include "video_send_pacing.h"

namespace px::transport {

// Bounds bytes waiting inside QUIC, separately from unacknowledged frames on the
// wire. The 64 KiB capacity matches TransportEndpoint::bind; a real-endpoint test
// guards this contract. Do not infer congestion solely from delayed feedback.
class VideoQueueAdmission final {
public:
    static constexpr std::uint64_t kTransportBufferBytes{64 * 1024};
    static constexpr std::uint64_t kBurstBytes{4 * 1100};

    explicit VideoQueueAdmission(VideoSendBudget budget = VideoSendBudget{})
        : budget_bytes_(std::clamp<std::uint64_t>(budget.VideoWireBitsPerSecond() * 16 / 8000, kBurstBytes, 32 * 1024)) {}

    [[nodiscard]] std::uint64_t BudgetBytes() const { return budget_bytes_; }
    [[nodiscard]] static std::uint64_t QueuedBytes(std::uint64_t available_bytes) {
        return kTransportBufferBytes - std::min(available_bytes, kTransportBufferBytes);
    }
    [[nodiscard]] bool CanSubmit(std::uint64_t available_bytes, std::uint64_t burst_bytes = kBurstBytes) const {
        return burst_bytes <= budget_bytes_ && QueuedBytes(available_bytes) <= budget_bytes_ - burst_bytes;
    }

private:
    std::uint64_t budget_bytes_{};
};
}  // namespace px::transport
