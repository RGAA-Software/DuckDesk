#ifndef TC_APPLICATION_CONCURRENT_QUEUE_H
#define TC_APPLICATION_CONCURRENT_QUEUE_H

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace px {

template <typename T>
class ConcurrentQueue {
public:
    [[nodiscard]] std::optional<T> Front() const {
        std::scoped_lock lock(mutex_);
        return values_.empty() ? std::nullopt : std::optional<T>{values_.front()};
    }

    [[nodiscard]] std::optional<T> Back() const {
        std::scoped_lock lock(mutex_);
        return values_.empty() ? std::nullopt : std::optional<T>{values_.back()};
    }

    [[nodiscard]] bool Empty() const {
        std::scoped_lock lock(mutex_);
        return values_.empty();
    }

    [[nodiscard]] std::size_t Size() const {
        std::scoped_lock lock(mutex_);
        return values_.size();
    }

    void PushBack(T value) {
        std::scoped_lock lock(mutex_);
        values_.push_back(std::move(value));
    }

    [[nodiscard]] std::optional<T> PopFrontValue() {
        std::scoped_lock lock(mutex_);
        if (values_.empty()) return std::nullopt;
        auto value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

    [[nodiscard]] std::optional<T> PopBackValue() {
        std::scoped_lock lock(mutex_);
        if (values_.empty()) return std::nullopt;
        auto value = std::move(values_.back());
        values_.pop_back();
        return value;
    }

    bool PopFront() {
        return PopFrontValue().has_value();
    }

    bool PopBack() {
        return PopBackValue().has_value();
    }

    [[nodiscard]] std::vector<T> ToVector() const {
        std::scoped_lock lock(mutex_);
        return {values_.begin(), values_.end()};
    }

    void Clear() {
        std::scoped_lock lock(mutex_);
        values_.clear();
    }

private:
    mutable std::mutex mutex_{};
    std::deque<T> values_{};
};

}  // namespace px

#endif  // TC_APPLICATION_CONCURRENT_QUEUE_H
