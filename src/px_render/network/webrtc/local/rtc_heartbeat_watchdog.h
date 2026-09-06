#pragma once

#include <atomic>
#include <cstdint>

namespace px {

class RtcHeartbeatWatchdog final {
  public:
    static constexpr std::int64_t kDefaultTimeoutMs = 15000;

    explicit RtcHeartbeatWatchdog(const std::int64_t timeout_ms = kDefaultTimeoutMs)
        : timeout_ms_(timeout_ms > 0 ? timeout_ms : kDefaultTimeoutMs) {}

    void Arm(const std::int64_t now_ms) {
        last_heartbeat_ms_.store(now_ms, std::memory_order_release);
    }

    void ObserveHeartbeat(const std::int64_t now_ms) {
        Arm(now_ms);
    }

    void Reset() {
        last_heartbeat_ms_.store(0, std::memory_order_release);
    }

    [[nodiscard]] bool IsArmed() const {
        return last_heartbeat_ms_.load(std::memory_order_acquire) > 0;
    }

    [[nodiscard]] bool HasExpired(const std::int64_t now_ms) const {
        const auto last_heartbeat_ms = last_heartbeat_ms_.load(std::memory_order_acquire);
        return last_heartbeat_ms > 0 && now_ms >= last_heartbeat_ms && now_ms - last_heartbeat_ms >= timeout_ms_;
    }

    [[nodiscard]] std::int64_t LastHeartbeatMs() const {
        return last_heartbeat_ms_.load(std::memory_order_acquire);
    }

  private:
    const std::int64_t timeout_ms_{kDefaultTimeoutMs};
    std::atomic<std::int64_t> last_heartbeat_ms_{0};
};

} // namespace px
