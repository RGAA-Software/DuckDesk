#ifndef PX_COMMON_NEW_RECONNECT_BACKOFF_H
#define PX_COMMON_NEW_RECONNECT_BACKOFF_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include "async_result.h"
#include "async_runtime.h"

namespace px {

struct PxReconnectBackoffOptions final {
    // Fixed retry interval. Connection recovery must not slow down after
    // repeated failures and must continue until its owner explicitly stops.
    std::chrono::milliseconds retry_delay{std::chrono::milliseconds(250)};
};

struct PxReconnectBackoffStep final {
    std::uint32_t attempt{0};
    std::chrono::milliseconds nominal_delay{0};
    std::chrono::milliseconds delay{0};
};

class PxReconnectBackoff final {
  public:
    static std::shared_ptr<PxReconnectBackoff> Create(PxReconnectBackoffOptions options = {});

    explicit PxReconnectBackoff(PxReconnectBackoffOptions options);

    PxReconnectBackoff(const PxReconnectBackoff&) = delete;
    PxReconnectBackoff& operator=(const PxReconnectBackoff&) = delete;

    [[nodiscard]] PxReconnectBackoffStep Next();
    void Reset();
    [[nodiscard]] std::uint32_t AttemptCount() const;

    [[nodiscard]] static PxAwaitable<PxResult<void>> Wait(std::chrono::milliseconds delay);

  private:
    const PxReconnectBackoffOptions options_;
    mutable std::mutex mutex_;
    std::uint32_t attempt_count_{0};
};

} // namespace px

#endif // PX_COMMON_NEW_RECONNECT_BACKOFF_H
