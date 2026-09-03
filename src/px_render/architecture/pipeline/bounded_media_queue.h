#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace px::render {

enum class QueueOverflowPolicy {
    kDropOldest,
    kDropNewest,
};

enum class QueueCloseMode {
    kDrain,
    kCancel,
};

enum class QueueSubmitResult {
    kAccepted,
    kAcceptedAfterDroppingOldest,
    kDroppedNewest,
    kRejectedClosed,
};

struct MediaQueueSnapshot final {
    std::size_t capacity{0};
    std::size_t depth{0};
    std::size_t high_watermark{0};
    std::uint64_t accepted{0};
    std::uint64_t dropped{0};
    std::uint64_t rejected_closed{0};
    bool closed{false};
};

// Lifetime:
// - Owned by one pipeline observer or sink.
// - Queued payloads use shared immutable ownership.
// - Close(kCancel) releases queued payloads immediately.
//
// Threading:
// - Multiple producers and one consumer are supported.
// - No mutex is held after Submit/TryPop/Close returns.
template<typename T>
class BoundedMediaQueue final {
public:
    BoundedMediaQueue(const std::size_t capacity,
                      const QueueOverflowPolicy overflow_policy)
        : capacity_(capacity), overflow_policy_(overflow_policy) {
        if (capacity_ == 0) {
            throw std::invalid_argument("media queue capacity must be positive");
        }
    }

    QueueSubmitResult Submit(std::shared_ptr<const T> item) {
        if (!item) {
            throw std::invalid_argument("media queue item must be owned");
        }
        const std::lock_guard lock(mutex_);
        if (closed_) {
            ++rejected_closed_;
            return QueueSubmitResult::kRejectedClosed;
        }
        if (items_.size() == capacity_) {
            ++dropped_;
            if (overflow_policy_ == QueueOverflowPolicy::kDropNewest) {
                return QueueSubmitResult::kDroppedNewest;
            }
            items_.pop_front();
            items_.push_back(std::move(item));
            ++accepted_;
            return QueueSubmitResult::kAcceptedAfterDroppingOldest;
        }
        items_.push_back(std::move(item));
        ++accepted_;
        high_watermark_ = std::max(high_watermark_, items_.size());
        return QueueSubmitResult::kAccepted;
    }

    [[nodiscard]] std::optional<std::shared_ptr<const T>> TryPop() {
        const std::lock_guard lock(mutex_);
        if (items_.empty()) {
            return std::nullopt;
        }
        auto item = std::move(items_.front());
        items_.pop_front();
        return item;
    }

    void Close(const QueueCloseMode mode) {
        const std::lock_guard lock(mutex_);
        closed_ = true;
        if (mode == QueueCloseMode::kCancel) {
            items_.clear();
        }
    }

    [[nodiscard]] MediaQueueSnapshot Snapshot() const {
        const std::lock_guard lock(mutex_);
        return MediaQueueSnapshot{
            .capacity = capacity_,
            .depth = items_.size(),
            .high_watermark = high_watermark_,
            .accepted = accepted_,
            .dropped = dropped_,
            .rejected_closed = rejected_closed_,
            .closed = closed_,
        };
    }

private:
    const std::size_t capacity_;
    const QueueOverflowPolicy overflow_policy_;
    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<const T>> items_;
    std::size_t high_watermark_{0};
    std::uint64_t accepted_{0};
    std::uint64_t dropped_{0};
    std::uint64_t rejected_closed_{0};
    bool closed_{false};
};

}  // namespace px::render
