#ifndef PX_COMMON_NEW_RECONNECT_BACKOFF_H
#define PX_COMMON_NEW_RECONNECT_BACKOFF_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>

#include "async_result.h"
#include "async_runtime.h"

namespace px {

struct PxReconnectBackoffOptions final {
    std::chrono::milliseconds initial_delay{std::chrono::milliseconds(250)};
    std::chrono::milliseconds maximum_delay{std::chrono::seconds(30)};
    double multiplier{2.0};
    double jitter_ratio{0.2};
    std::optional<std::uint64_t> random_seed{};
};

struct PxReconnectBackoffStep final {
    std::uint32_t attempt{0};
    std::chrono::milliseconds nominal_delay{0};
    std::chrono::milliseconds delay{0};
};

class PxReconnectBackoff final {
  public:
    static std::shared_ptr<PxReconnectBackoff> Create(PxReconnectBackoffOptions options = {});

    explicit PxReconnectBackoff(PxReconnectBackoffOptions options, std::uint64_t seed);

    PxReconnectBackoff(const PxReconnectBackoff&) = delete;
    PxReconnectBackoff& operator=(const PxReconnectBackoff&) = delete;

    [[nodiscard]] PxReconnectBackoffStep Next();
    void Reset();
    [[nodiscard]] std::uint32_t AttemptCount() const;

    [[nodiscard]] static PxAwaitable<PxResult<void>> Wait(std::chrono::milliseconds delay);

  private:
    [[nodiscard]] std::chrono::milliseconds NominalDelay(std::uint32_t attempt) const;

    const PxReconnectBackoffOptions options_;
    mutable std::mutex mutex_;
    std::mt19937_64 random_;
    std::uint32_t attempt_count_{0};
};

} // namespace px

#endif // PX_COMMON_NEW_RECONNECT_BACKOFF_H
