#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace px::transport {

// Caller serializes access. Credit covers queued AND QUIC-buffered bytes until the peer accepts them.
class FileSendWindow final {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t kWindowBytes{256 * 1024};
    static constexpr std::size_t kCongestedWindowBytes{128 * 1024};
    static constexpr std::size_t kMaximumMessage{1024 * 1024};

    [[nodiscard]] bool CanReserve(std::size_t bytes) const {
        if (bytes == 0 || bytes > kMaximumMessage || reserved_bytes_ > std::numeric_limits<std::uint64_t>::max() - bytes) return false;
        const auto outstanding = OutstandingBytes();
        // Preserve the existing maximum legal message, but send an oversized message alone.
        return outstanding == 0 || (outstanding < window_bytes_ && bytes <= window_bytes_ - outstanding);
    }
    [[nodiscard]] bool Reserve(std::size_t bytes, Clock::time_point now = Clock::now()) {
        if (!CanReserve(bytes)) return false;
        reserved_bytes_ += bytes;
        // One outstanding timing sample is enough; metadata-heavy traffic cannot grow a sample queue.
        if (!receipt_sample_) receipt_sample_ = ReceiptSample{reserved_bytes_, now};
        return true;
    }
    // nullopt rejects regressions/receipts beyond accepted sends; duplicate receipts are harmless.
    [[nodiscard]] std::optional<bool> Acknowledge(std::uint64_t received_bytes, Clock::time_point now = Clock::now()) {
        if (received_bytes < acknowledged_bytes_ || received_bytes > reserved_bytes_) return std::nullopt;
        const bool advanced = received_bytes != acknowledged_bytes_;
        acknowledged_bytes_ = received_bytes;
        if (advanced && receipt_sample_ && received_bytes >= receipt_sample_->through_bytes) {
            ObserveReceiptDelay(now - receipt_sample_->reserved_at, now);
            receipt_sample_.reset();
        }
        return advanced;
    }
    [[nodiscard]] std::uint64_t OutstandingBytes() const { return reserved_bytes_ - acknowledged_bytes_; }
    [[nodiscard]] std::size_t WindowBytes() const { return window_bytes_; }
    [[nodiscard]] std::int64_t ReceiptDelayMicros() const { return receipt_delay_us_; }

private:
    void ObserveReceiptDelay(Clock::duration delay, Clock::time_point now) {
        if (delay < Clock::duration::zero()) return;
        receipt_delay_us_ = std::chrono::duration_cast<std::chrono::microseconds>(delay).count();
        baseline_delay_ = baseline_delay_ ? std::min(*baseline_delay_, delay) : delay;
        const auto growth = delay - *baseline_delay_;
        // Yield one normal block of bulk credit before media's 100 ms congestion threshold.
        // Existing debt is retained. Control, input and datagrams never wait for this window.
        if (growth >= std::chrono::milliseconds(50)) {
            window_bytes_ = kCongestedWindowBytes;
            healthy_since_.reset();
        } else if (growth <= std::chrono::milliseconds(25)) {
            if (!healthy_since_) healthy_since_ = now;
            if (now - *healthy_since_ >= std::chrono::seconds(1)) window_bytes_ = kWindowBytes;
        } else {
            healthy_since_.reset();
        }
    }
    struct ReceiptSample final {
        std::uint64_t through_bytes{};
        Clock::time_point reserved_at{};
    };
    std::uint64_t reserved_bytes_{};
    std::uint64_t acknowledged_bytes_{};
    std::size_t window_bytes_{kWindowBytes};
    std::optional<ReceiptSample> receipt_sample_{};
    std::optional<Clock::duration> baseline_delay_{};
    std::optional<Clock::time_point> healthy_since_{};
    std::int64_t receipt_delay_us_{};
};
}  // namespace px::transport
