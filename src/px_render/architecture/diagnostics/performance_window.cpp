#include "diagnostics/performance_window.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace px::render {

PerformanceWindow::PerformanceWindow(const std::size_t capacity)
    : capacity_(capacity) {
    if (capacity_ == 0) {
        throw std::invalid_argument("performance window capacity must be positive");
    }
}

void PerformanceWindow::Observe(const std::int64_t duration_us) {
    const std::lock_guard lock(mutex_);
    ++total_observations_;
    if (samples_.size() == capacity_) {
        samples_.pop_front();
        ++evicted_samples_;
    }
    samples_.push_back(std::max<std::int64_t>(duration_us, 0));
}

PerformanceSnapshot PerformanceWindow::Snapshot() const {
    const std::lock_guard lock(mutex_);
    PerformanceSnapshot snapshot{
        .total_observations = total_observations_,
        .sample_count = samples_.size(),
        .evicted_samples = evicted_samples_,
    };
    if (samples_.empty()) {
        return snapshot;
    }

    const auto sum = std::accumulate(
        samples_.begin(), samples_.end(), std::int64_t{0});
    snapshot.minimum_us = *std::min_element(samples_.begin(), samples_.end());
    snapshot.maximum_us = *std::max_element(samples_.begin(), samples_.end());
    snapshot.average_us = sum / static_cast<std::int64_t>(samples_.size());

    std::vector<std::int64_t> ordered(samples_.begin(), samples_.end());
    std::sort(ordered.begin(), ordered.end());
    const auto percentile = [&ordered](const std::size_t rank) {
        const auto index = (ordered.size() * rank + 99U) / 100U - 1U;
        return ordered.at(index);
    };
    snapshot.p50_us = percentile(50U);
    snapshot.p95_us = percentile(95U);
    snapshot.p99_us = percentile(99U);
    return snapshot;
}

void PerformanceWindow::Reset() {
    const std::lock_guard lock(mutex_);
    samples_.clear();
    total_observations_ = 0;
    evicted_samples_ = 0;
}

}  // namespace px::render
