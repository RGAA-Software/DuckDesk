#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace px::render {

struct PerformanceSnapshot final {
    std::uint64_t total_observations{0};
    std::size_t sample_count{0};
    std::uint64_t evicted_samples{0};
    std::int64_t minimum_us{0};
    std::int64_t maximum_us{0};
    std::int64_t average_us{0};
    std::int64_t p50_us{0};
    std::int64_t p95_us{0};
    std::int64_t p99_us{0};
};

// Lifetime:
// - Owned by its diagnostics module.
// - Contains only bounded value samples and owns no callback.
//
// Threading:
// - Observe and Snapshot may be called concurrently.
// - No lock is exposed or held across an external call.
class PerformanceWindow final {
public:
    explicit PerformanceWindow(std::size_t capacity);

    void Observe(std::int64_t duration_us);
    [[nodiscard]] PerformanceSnapshot Snapshot() const;
    void Reset();

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<std::int64_t> samples_;
    std::uint64_t total_observations_{0};
    std::uint64_t evicted_samples_{0};
};

}  // namespace px::render
