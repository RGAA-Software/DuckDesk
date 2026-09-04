#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace px::render {

struct TransportPerformanceSnapshot final {
    std::uint64_t window_ms{0};
    std::uint64_t inbound_messages{0};
    std::uint64_t inbound_bytes{0};
    std::uint64_t outbound_messages{0};
    std::uint64_t outbound_bytes{0};
    std::uint64_t dropped_messages{0};
    std::uint64_t connected{0};
    std::uint64_t disconnected{0};
    std::size_t active_connections{0};
    std::size_t queue_depth{0};
    std::size_t queue_high_watermark{0};
};

// High-frequency transport callbacks perform atomic accounting only. The
// control lane periodically snapshots and resets the bounded window.
class TransportPerformanceWindow final {
public:
    explicit TransportPerformanceWindow(
        std::chrono::steady_clock::duration interval =
            std::chrono::seconds(5),
        std::chrono::steady_clock::time_point start = {});

    void ObserveInbound(std::size_t bytes) noexcept;
    void ObserveOutbound(std::size_t bytes) noexcept;
    void ObserveDropped() noexcept;
    void ObserveConnected() noexcept;
    void ObserveDisconnected() noexcept;
    void ObserveQueueDepth(std::size_t depth) noexcept;

    [[nodiscard]] std::optional<TransportPerformanceSnapshot>
    SnapshotAndReset(
        std::chrono::steady_clock::time_point now,
        std::size_t active_connections,
        std::size_t queue_depth);
    void Reset(std::chrono::steady_clock::time_point now) noexcept;

private:
    const std::chrono::steady_clock::duration interval_;
    mutable std::mutex window_mutex_;
    std::chrono::steady_clock::time_point window_start_;
    std::atomic_uint64_t inbound_messages_{0};
    std::atomic_uint64_t inbound_bytes_{0};
    std::atomic_uint64_t outbound_messages_{0};
    std::atomic_uint64_t outbound_bytes_{0};
    std::atomic_uint64_t dropped_messages_{0};
    std::atomic_uint64_t connected_{0};
    std::atomic_uint64_t disconnected_{0};
    std::atomic_size_t queue_high_watermark_{0};
};

}  // namespace px::render
