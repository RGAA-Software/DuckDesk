#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace px::transport {

// Caller serializes access. Credit covers queued AND QUIC-buffered bytes until the peer accepts them.
class FileSendWindow final {
public:
    static constexpr std::size_t kWindowBytes{256 * 1024};
    static constexpr std::size_t kMaximumMessage{1024 * 1024};

    [[nodiscard]] bool CanReserve(std::size_t bytes) const {
        if (bytes == 0 || bytes > kMaximumMessage || reserved_bytes_ > std::numeric_limits<std::uint64_t>::max() - bytes) return false;
        const auto outstanding = OutstandingBytes();
        // Preserve the existing maximum legal message, but send an oversized message alone.
        return outstanding == 0 || (outstanding < kWindowBytes && bytes <= kWindowBytes - outstanding);
    }
    [[nodiscard]] bool Reserve(std::size_t bytes) {
        if (!CanReserve(bytes)) return false;
        reserved_bytes_ += bytes;
        return true;
    }
    // nullopt rejects regressions/receipts beyond accepted sends; duplicate receipts are harmless.
    [[nodiscard]] std::optional<bool> Acknowledge(std::uint64_t received_bytes) {
        if (received_bytes < acknowledged_bytes_ || received_bytes > reserved_bytes_) return std::nullopt;
        const bool advanced = received_bytes != acknowledged_bytes_;
        acknowledged_bytes_ = received_bytes;
        return advanced;
    }
    [[nodiscard]] std::uint64_t OutstandingBytes() const { return reserved_bytes_ - acknowledged_bytes_; }

private:
    std::uint64_t reserved_bytes_{};
    std::uint64_t acknowledged_bytes_{};
};
}  // namespace px::transport
