#ifndef PX_CONCURRENT_TYPE_H
#define PX_CONCURRENT_TYPE_H

#include <concepts>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace px {

template <typename T>
class ConcurrentType {
public:
    ConcurrentType() = default;
    explicit ConcurrentType(T value) : value_(std::move(value)) {}

    ConcurrentType(const ConcurrentType& other) : value_(other.Clone()) {}

    void Update(T value) {
        std::scoped_lock lock(mutex_);
        value_ = std::move(value);
    }

    [[nodiscard]] T Clone() const {
        std::scoped_lock lock(mutex_);
        return value_;
    }

    [[nodiscard]] bool HasValue() const
        requires requires(const T& value) { static_cast<bool>(value); }
    {
        std::scoped_lock lock(mutex_);
        return static_cast<bool>(value_);
    }

    template <typename Function>
        requires std::invocable<Function&, T&>
    decltype(auto) WithLock(Function&& function) {
        std::scoped_lock lock(mutex_);
        return std::invoke(std::forward<Function>(function), value_);
    }

    ConcurrentType& operator=(const ConcurrentType& other) {
        if (this != std::addressof(other)) Update(other.Clone());
        return *this;
    }

    ConcurrentType& operator=(T value) {
        Update(std::move(value));
        return *this;
    }

    [[nodiscard]] bool operator==(const T& other) const {
        return Clone() == other;
    }

private:
    mutable std::mutex mutex_{};
    T value_{};
};

using ConcurrentString = ConcurrentType<std::string>;

template <typename T>
using Mutex = ConcurrentType<T>;

}  // namespace px

#endif  // PX_CONCURRENT_TYPE_H
