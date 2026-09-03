#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace px::render {

struct RateLimitDecision final {
    bool emit{false};
    std::uint64_t suppressed_since_last_emit{0};
};

// Lifetime:
// - Owned by the diagnostics module.
// - Key state is value-owned and bounded by max_keys.
//
// Threading:
// - Evaluate and TrackedKeyCount may be called concurrently.
// - Callers emit logs only after Evaluate returns; no logger runs under lock.
class RateLimitedLogGate final {
public:
    RateLimitedLogGate(std::chrono::steady_clock::duration interval,
                       std::size_t max_keys);

    [[nodiscard]] RateLimitDecision Evaluate(
        std::string key,
        std::chrono::steady_clock::time_point now);
    [[nodiscard]] std::size_t TrackedKeyCount() const;

private:
    struct KeyState final {
        std::chrono::steady_clock::time_point last_emit;
        std::uint64_t suppressed{0};
        std::uint64_t last_touch{0};
    };

    void EvictLeastRecentlyUsed();

    const std::chrono::steady_clock::duration interval_;
    const std::size_t max_keys_;
    mutable std::mutex mutex_;
    std::map<std::string, KeyState> keys_;
    std::uint64_t touch_sequence_{0};
};

}  // namespace px::render
