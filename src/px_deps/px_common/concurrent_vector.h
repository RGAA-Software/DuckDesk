#ifndef TC_APPLICATION_CONCURRENT_VECTOR_H
#define TC_APPLICATION_CONCURRENT_VECTOR_H

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace px {

template <typename T>
class ConcurrentVector {
public:
    void PushBack(T value) {
        std::scoped_lock lock(mutex_);
        values_.push_back(std::move(value));
    }

    [[nodiscard]] std::size_t Size() const {
        std::scoped_lock lock(mutex_);
        return values_.size();
    }

    [[nodiscard]] bool Empty() const {
        return Size() == 0;
    }

    void Resize(std::size_t size) {
        std::scoped_lock lock(mutex_);
        values_.resize(size);
    }

    [[nodiscard]] std::optional<T> At(std::size_t index) const {
        std::scoped_lock lock(mutex_);
        return index < values_.size() ? std::optional<T>{values_[index]} : std::nullopt;
    }

    template <typename Callback>
        requires std::invocable<Callback&, const T&>
    void Visit(Callback&& callback) const {
        const auto snapshot = Clone();
        for (const auto& value : snapshot) std::invoke(callback, value);
    }

    [[nodiscard]] std::vector<T> Clone() const {
        std::scoped_lock lock(mutex_);
        return values_;
    }

    [[nodiscard]] std::optional<T> PopFront() {
        std::scoped_lock lock(mutex_);
        if (values_.empty()) return std::nullopt;
        auto value = std::move(values_.front());
        values_.erase(values_.begin());
        return value;
    }

    void RemoveFirst() {
        static_cast<void>(PopFront());
    }

    void Clear() {
        std::scoped_lock lock(mutex_);
        values_.clear();
    }

    template <std::ranges::input_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, T>
    void CopyFrom(const Range& source) {
        std::scoped_lock lock(mutex_);
        values_.assign(std::ranges::begin(source), std::ranges::end(source));
    }

    template <std::ranges::input_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, T>
    bool CopyMemFrom(const Range& source) {
        CopyFrom(source);
        return true;
    }

    template <std::ranges::sized_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, T>
    bool CopyMemPartialFrom(const Range& source, std::size_t size) {
        if (std::ranges::size(source) < size) return false;
        std::scoped_lock lock(mutex_);
        auto end = std::ranges::begin(source);
        std::ranges::advance(end, static_cast<std::ptrdiff_t>(size));
        values_.assign(std::ranges::begin(source), end);
        return true;
    }

    void CopyMemTo(std::vector<T>& output) const {
        output = Clone();
    }

private:
    mutable std::mutex mutex_{};
    std::vector<T> values_{};
};

}  // namespace px

#endif  // TC_APPLICATION_CONCURRENT_VECTOR_H
