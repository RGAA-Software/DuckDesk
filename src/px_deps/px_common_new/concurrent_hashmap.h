//
// Created by RGAA on 2024-02-23.
//

#ifndef TC_APPLICATION_CONCURRENT_HASHMAP_H
#define TC_APPLICATION_CONCURRENT_HASHMAP_H

#include <map>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <optional>
#include <vector>
#include "log.h"

namespace px
{

    template<class K, class V>
    class ConcurrentHashMap {
    public:

        void Insert(const K& k, const V& v) {
            std::lock_guard<std::mutex> lock(mtx_);
            inner_[k] = v;
        }

        void BatchInsert(const std::map<K, V>& values) {
            std::lock_guard<std::mutex> lock(mtx_);
            for (const auto& [k, v] : values) {
                inner_[k] = v;
            }
        }

        void ClearAndBatchInsert(const std::map<K, V>& values) {
            std::lock_guard<std::mutex> lock(mtx_);
            inner_.clear();
            for (const auto& [k, v] : values) {
                inner_[k] = v;
            }
        }

        void Replace(const K& k, const V& v) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (inner_.contains(k)) {
                inner_.erase(k);
            }
            inner_[k] = v;
        }

        std::optional<V> Remove(const K& k) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = inner_.find(k);
            if (it != inner_.end()) {
                auto v = inner_[k];
                inner_.erase(it);
                return v;
            }
            return std::nullopt;
        }

        // 仅当 key 存在且当前值与 predicate 匹配时才移除。
        // 用于 endpoint 字符串复用等场景,避免旧的异步断开回调把新会话误删。
        std::optional<V> RemoveIf(const K& k, std::function<bool(const V&)>&& predicate) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = inner_.find(k);
            if (it != inner_.end() && predicate(it->second)) {
                auto v = it->second;
                inner_.erase(it);
                return v;
            }
            return std::nullopt;
        }

        bool HasKey(const K& k) {
            std::lock_guard<std::mutex> lock(mtx_);
            return inner_.find(k) != inner_.end();
        }

        V Get(const K& k) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = inner_.find(k);
            if (it == inner_.end()) {
                LOGE("ConcurrentHashMap::Get missing key");
                return V{};
            }
            return it->second;
        }

        std::optional<V> TryGet(const K& k) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (inner_.find(k) != inner_.end()) {
                return inner_.at(k);
            }
            return std::nullopt;
        }

        void Apply(const K& k, std::function<void(const V& v)>&& task) {
            std::optional<V> value;
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (inner_.find(k) != inner_.end()) {
                    value = inner_.at(k);
                }
            }
            if (value.has_value()) {
                task(value.value());
            }
        }

        void ApplyAll(std::function<void(const K&k, const V& v)>&& task) {
            auto snapshot = Clone();
            for (const auto& [k, v] : snapshot) {
                task(k, v);
            }
        }

        void ApplyAllCond(std::function<bool(const K& k, const V& v)>&& task) {
            auto snapshot = Clone();
            for (auto& [k, v] : snapshot) {
                if (task(k, v)) {
                    break;
                }
            }
        }

        void VisitAll(std::function<void(K k, V& v)>&& task) {
            auto keys = Keys();
            for (const auto& k : keys) {
                std::optional<V> value;
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    auto it = inner_.find(k);
                    if (it == inner_.end()) {
                        continue;
                    }
                    value = it->second;
                }
                task(k, value.value());
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    auto it = inner_.find(k);
                    if (it != inner_.end()) {
                        it->second = value.value();
                    }
                }
            }
        }

        void VisitAllCond(std::function<bool(K k, V& v)>&& task) {
            auto keys = Keys();
            for (const auto& k : keys) {
                std::optional<V> value;
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    auto it = inner_.find(k);
                    if (it == inner_.end()) {
                        continue;
                    }
                    value = it->second;
                }
                bool should_break = task(k, value.value());
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    auto it = inner_.find(k);
                    if (it != inner_.end()) {
                        it->second = value.value();
                    }
                }
                if (should_break) {
                    break;
                }
            }
        }

        // 0-based
        std::optional<std::vector<V>> QueryRange(int begin, int end) {
            std::lock_guard<std::mutex> lock(mtx_);
            std::vector<V> values;
            // overflow
            if (begin >= inner_.size()) {
                //LOGI("Overflow, begin: {}, total: {}", begin, inner_.size());
                return std::nullopt;
            }

            auto it_beg = inner_.begin();
            std::advance(it_beg, begin);

            decltype(it_beg) it_end;

            if (begin < inner_.size() && end >= inner_.size()) {
                // portion
                it_end = inner_.end();
                //LOGI("Portion, {} -> {}", begin, inner_.size());
            }
            else {
                // whole
                it_end = it_beg;
                std::advance(it_end, end - begin);
                //LOGI("Whole");
            }
            for (; it_beg != it_end; ++it_beg) {
                auto pair = *it_beg;
                values.push_back(pair.second);
                //LOGI("--> {}", pair.first);
            }
            return values;
        }

        size_t Size() {
            std::lock_guard<std::mutex> lock(mtx_);
            return inner_.size();
        }

        bool Empty() {
            return Size() <= 0;
        }

        void Clear() {
            std::lock_guard<std::mutex> lock(mtx_);
            inner_.clear();
        }

        std::map<K, V> Clone() {
            std::lock_guard<std::mutex> lock(mtx_);
            std::map<K,V> cpy;
            for (const auto& [k, v] : inner_) {
                cpy.insert({k, v});
            }
            return cpy;
        }

        std::vector<K> Keys() {
            std::lock_guard<std::mutex> lock(mtx_);
            std::vector<K> keys;
            keys.reserve(inner_.size());
            for (const auto& [k, _] : inner_) {
                keys.push_back(k);
            }
            return keys;
        }

    private:
        std::mutex mtx_;
        std::map<K,V> inner_;
    };

}

#endif //TC_APPLICATION_CONCURRENT_HASHMAP_H
