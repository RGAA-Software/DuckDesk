#include "diagnostics/rate_limited_log.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace px::render {

RateLimitedLogGate::RateLimitedLogGate(
    const std::chrono::steady_clock::duration interval,
    const std::size_t max_keys)
    : interval_(interval), max_keys_(max_keys) {
    if (interval_ <= std::chrono::steady_clock::duration::zero()) {
        throw std::invalid_argument("rate limit interval must be positive");
    }
    if (max_keys_ == 0) {
        throw std::invalid_argument("rate limit key capacity must be positive");
    }
}

RateLimitDecision RateLimitedLogGate::Evaluate(
    std::string key,
    const std::chrono::steady_clock::time_point now) {
    if (key.empty()) {
        throw std::invalid_argument("rate limit key must not be empty");
    }

    const std::lock_guard lock(mutex_);
    ++touch_sequence_;
    auto found = keys_.find(key);
    if (found == keys_.end()) {
        if (keys_.size() == max_keys_) {
            EvictLeastRecentlyUsed();
        }
        keys_.emplace(std::move(key), KeyState{
            .last_emit = now,
            .suppressed = 0,
            .last_touch = touch_sequence_,
        });
        return {.emit = true, .suppressed_since_last_emit = 0};
    }

    auto& state = found->second;
    state.last_touch = touch_sequence_;
    if (now < state.last_emit || now - state.last_emit >= interval_) {
        const auto suppressed = state.suppressed;
        state.last_emit = now;
        state.suppressed = 0;
        return {.emit = true, .suppressed_since_last_emit = suppressed};
    }

    ++state.suppressed;
    return {.emit = false, .suppressed_since_last_emit = 0};
}

std::size_t RateLimitedLogGate::TrackedKeyCount() const {
    const std::lock_guard lock(mutex_);
    return keys_.size();
}

void RateLimitedLogGate::EvictLeastRecentlyUsed() {
    const auto oldest = std::min_element(
        keys_.begin(), keys_.end(), [](const auto& left, const auto& right) {
            return left.second.last_touch < right.second.last_touch;
        });
    if (oldest != keys_.end()) {
        keys_.erase(oldest);
    }
}

}  // namespace px::render
