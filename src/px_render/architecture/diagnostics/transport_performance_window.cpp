#include "diagnostics/transport_performance_window.h"

#include <algorithm>
#include <stdexcept>

namespace px::render {

TransportPerformanceWindow::TransportPerformanceWindow(
    const std::chrono::steady_clock::duration interval,
    const std::chrono::steady_clock::time_point start)
    : interval_(interval), window_start_(start) {
    if (interval_ <= std::chrono::steady_clock::duration::zero()) {
        throw std::invalid_argument("transport performance interval must be positive");
    }
}

void TransportPerformanceWindow::ObserveInbound(const std::size_t bytes) noexcept {
    inbound_messages_.fetch_add(1, std::memory_order_relaxed);
    inbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void TransportPerformanceWindow::ObserveOutbound(const std::size_t bytes) noexcept {
    outbound_messages_.fetch_add(1, std::memory_order_relaxed);
    outbound_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void TransportPerformanceWindow::ObserveDropped() noexcept {
    dropped_messages_.fetch_add(1, std::memory_order_relaxed);
}

void TransportPerformanceWindow::ObserveConnected() noexcept {
    connected_.fetch_add(1, std::memory_order_relaxed);
}

void TransportPerformanceWindow::ObserveDisconnected() noexcept {
    disconnected_.fetch_add(1, std::memory_order_relaxed);
}

void TransportPerformanceWindow::ObserveQueueDepth(const std::size_t depth) noexcept {
    auto high = queue_high_watermark_.load(std::memory_order_relaxed);
    while (depth > high && !queue_high_watermark_.compare_exchange_weak(
               high, depth, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

std::optional<TransportPerformanceSnapshot>
TransportPerformanceWindow::SnapshotAndReset(
    const std::chrono::steady_clock::time_point now,
    const std::size_t active_connections,
    const std::size_t queue_depth) {
    std::lock_guard lock(window_mutex_);
    const auto elapsed = now - window_start_;
    if (elapsed < interval_) {
        ObserveQueueDepth(queue_depth);
        return std::nullopt;
    }
    ObserveQueueDepth(queue_depth);
    const auto elapsed_ms = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    TransportPerformanceSnapshot snapshot{
        .window_ms = static_cast<std::uint64_t>(elapsed_ms),
        .inbound_messages = inbound_messages_.exchange(0, std::memory_order_acq_rel),
        .inbound_bytes = inbound_bytes_.exchange(0, std::memory_order_acq_rel),
        .outbound_messages = outbound_messages_.exchange(0, std::memory_order_acq_rel),
        .outbound_bytes = outbound_bytes_.exchange(0, std::memory_order_acq_rel),
        .dropped_messages = dropped_messages_.exchange(0, std::memory_order_acq_rel),
        .connected = connected_.exchange(0, std::memory_order_acq_rel),
        .disconnected = disconnected_.exchange(0, std::memory_order_acq_rel),
        .active_connections = active_connections,
        .queue_depth = queue_depth,
        .queue_high_watermark = queue_high_watermark_.exchange(
            queue_depth, std::memory_order_acq_rel),
    };
    window_start_ = now;
    return snapshot;
}

void TransportPerformanceWindow::Reset(
    const std::chrono::steady_clock::time_point now) noexcept {
    std::lock_guard lock(window_mutex_);
    window_start_ = now;
    inbound_messages_.store(0, std::memory_order_relaxed);
    inbound_bytes_.store(0, std::memory_order_relaxed);
    outbound_messages_.store(0, std::memory_order_relaxed);
    outbound_bytes_.store(0, std::memory_order_relaxed);
    dropped_messages_.store(0, std::memory_order_relaxed);
    connected_.store(0, std::memory_order_relaxed);
    disconnected_.store(0, std::memory_order_relaxed);
    queue_high_watermark_.store(0, std::memory_order_relaxed);
}

}  // namespace px::render
