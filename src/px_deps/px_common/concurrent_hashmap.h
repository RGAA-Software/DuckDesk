#ifndef TC_APPLICATION_CONCURRENT_HASHMAP_H
#define TC_APPLICATION_CONCURRENT_HASHMAP_H

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#include "log.h"

namespace px {

// Compatibility name retained for callers. The deterministic ordered backing store is intentional.
// Apply/ApplyAll run against value snapshots and never hold the mutex while calling user code.
// VisitAll/VisitAllCond mutate stored values and therefore hold the mutex for the callback duration.
template <class K, class V>
class ConcurrentHashMap {
public:
    void Insert(K key, V value) {
        std::scoped_lock lock(mutex_);
        values_.insert_or_assign(std::move(key), std::move(value));
    }

    void BatchInsert(const std::map<K, V>& values) {
        std::scoped_lock lock(mutex_);
        for (const auto& [key, value] : values) values_.insert_or_assign(key, value);
    }

    void ClearAndBatchInsert(const std::map<K, V>& values) {
        std::scoped_lock lock(mutex_);
        values_ = values;
    }

    void Replace(const K& key, const V& value) {
        Insert(key, value);
    }

    [[nodiscard]] std::optional<V> Remove(const K& key) {
        std::scoped_lock lock(mutex_);
        const auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        auto removed = std::move(it->second);
        values_.erase(it);
        return removed;
    }

    template <typename Predicate>
        requires std::predicate<Predicate&, const V&>
    [[nodiscard]] std::optional<V> RemoveIf(const K& key, Predicate&& predicate) {
        std::scoped_lock lock(mutex_);
        const auto it = values_.find(key);
        if (it == values_.end() || !std::invoke(predicate, std::as_const(it->second))) return std::nullopt;
        auto removed = std::move(it->second);
        values_.erase(it);
        return removed;
    }

    [[nodiscard]] bool HasKey(const K& key) const {
        std::scoped_lock lock(mutex_);
        return values_.contains(key);
    }

    [[nodiscard]] V Get(const K& key) const {
        const auto value = TryGet(key);
        if (!value) {
            LOGE("ConcurrentHashMap::Get missing key");
            return V{};
        }
        return *value;
    }

    [[nodiscard]] std::optional<V> TryGet(const K& key) const {
        std::scoped_lock lock(mutex_);
        const auto it = values_.find(key);
        return it == values_.end() ? std::nullopt : std::optional<V>{it->second};
    }

    template <typename Task>
        requires std::invocable<Task&, const V&>
    void Apply(const K& key, Task&& task) const {
        const auto value = TryGet(key);
        if (value) std::invoke(task, *value);
    }

    template <typename Task>
        requires std::invocable<Task&, const K&, const V&>
    void ApplyAll(Task&& task) const {
        const auto snapshot = Clone();
        for (const auto& [key, value] : snapshot) std::invoke(task, key, value);
    }

    template <typename Task>
        requires std::predicate<Task&, const K&, const V&>
    void ApplyAllCond(Task&& task) const {
        const auto snapshot = Clone();
        for (const auto& [key, value] : snapshot) {
            if (std::invoke(task, key, value)) break;
        }
    }

    template <typename Task>
        requires std::invocable<Task&, const K&, V&>
    void VisitAll(Task&& task) {
        std::scoped_lock lock(mutex_);
        for (auto& [key, value] : values_) std::invoke(task, std::as_const(key), value);
    }

    template <typename Task>
        requires std::predicate<Task&, const K&, V&>
    void VisitAllCond(Task&& task) {
        std::scoped_lock lock(mutex_);
        for (auto& [key, value] : values_) {
            if (std::invoke(task, std::as_const(key), value)) break;
        }
    }

    [[nodiscard]] std::optional<std::vector<V>> QueryRange(std::size_t begin, std::size_t end) const {
        std::scoped_lock lock(mutex_);
        if (begin >= end || begin >= values_.size()) return std::nullopt;
        end = std::min(end, values_.size());
        std::vector<V> result{};
        result.reserve(end - begin);
        auto iterator = values_.begin();
        std::advance(iterator, static_cast<std::ptrdiff_t>(begin));
        for (auto index = begin; index < end; ++index, ++iterator) result.push_back(iterator->second);
        return result;
    }

    [[nodiscard]] std::size_t Size() const {
        std::scoped_lock lock(mutex_);
        return values_.size();
    }

    [[nodiscard]] bool Empty() const {
        return Size() == 0;
    }

    void Clear() {
        std::scoped_lock lock(mutex_);
        values_.clear();
    }

    [[nodiscard]] std::map<K, V> Clone() const {
        std::scoped_lock lock(mutex_);
        return values_;
    }

    [[nodiscard]] std::vector<K> Keys() const {
        std::scoped_lock lock(mutex_);
        std::vector<K> keys{};
        keys.reserve(values_.size());
        for (const auto& key : std::views::keys(values_)) keys.push_back(key);
        return keys;
    }

private:
    mutable std::mutex mutex_{};
    std::map<K, V> values_{};
};

}  // namespace px

#endif  // TC_APPLICATION_CONCURRENT_HASHMAP_H
