//
// Created by RGAA on 2024/2/27.
//

#ifndef TC_APPLICATION_CONCURRENT_VECTOR_H
#define TC_APPLICATION_CONCURRENT_VECTOR_H

#include <cstring>
#include <mutex>
#include <type_traits>
#include <vector>
#include <functional>

namespace px
{

    template<typename T>
    class ConcurrentVector {
    public:
        void PushBack(const T& t) {
            std::lock_guard<std::mutex> guard(mtx_);
            inner_.push_back(t);
        }

        int Size() {
            std::lock_guard<std::mutex> guard(mtx_);
            return inner_.size();
        }

        void Resize(std::size_t size) {
            std::lock_guard<std::mutex> guard(mtx_);
            inner_.resize(size);
        }

        T At(int idx) {
            std::lock_guard<std::mutex> guard(mtx_);
            return inner_.at(idx);
        }

        void Visit(std::function<void(const T&)>&& cbk) {
            std::lock_guard<std::mutex> guard(mtx_);
            for (const auto& t : inner_) {
                cbk(t);
            }
        }

        std::vector<T> Clone() {
            std::lock_guard<std::mutex> guard(mtx_);
            std::vector<T> out;
            for (const auto& t : inner_) {
                out.push_back(t);
            }
            return out;
        }

        void RemoveFirst() {
            std::lock_guard<std::mutex> guard(mtx_);
            if (!inner_.empty()) {
                inner_.erase(inner_.begin());
            }
        }

        void Clear() {
            std::lock_guard<std::mutex> guard(mtx_);
            inner_.clear();
        }

        void CopyFrom(const std::vector<T>& f) {
            std::lock_guard<std::mutex> guard(mtx_);
            inner_.clear();
            inner_.insert(inner_.begin(), f.begin(), f.end());
        }

        template<typename From,
                typename = std::enable_if_t<std::is_same_v<T, typename From::value_type>>>
        void CopyFrom(const From& f) {
            if (!std::is_same_v<T, typename From::value_type>) {
                return;
            }
            std::lock_guard<std::mutex> guard(mtx_);
            inner_.clear();
            inner_.insert(inner_.begin(), f.begin(), f.end());
        }

        bool CopyMemFrom(const std::vector<T>& f) {
            static_assert(std::is_trivially_copyable_v<T>, "CopyMemFrom requires trivially copyable T");
            std::lock_guard<std::mutex> guard(mtx_);
            if (inner_.size() < f.size()) {
                inner_.resize(f.size());
            }
            memcpy(inner_.data(), f.data(), f.size() * sizeof(T));
            return true;
        }

        bool CopyMemPartialFrom(const std::vector<T>& f, int size) {
            static_assert(std::is_trivially_copyable_v<T>, "CopyMemPartialFrom requires trivially copyable T");
            std::lock_guard<std::mutex> guard(mtx_);
            if (f.size() < size) {
                return false;
            }
            if (inner_.size() < size) {
                inner_.resize(size);
            }
            memcpy(inner_.data(), f.data(), size * sizeof(T));
            return true;
        }

        template<typename From,
                typename = std::enable_if_t<std::is_same_v<T, typename From::value_type>>>
        bool CopyMemFrom(const From& f) {
            static_assert(std::is_trivially_copyable_v<T>, "CopyMemFrom requires trivially copyable T");
            std::lock_guard<std::mutex> guard(mtx_);
            if (inner_.size() < f.size()) {
                return false;
            }
            memcpy(inner_.data(), f.data(), f.size() * sizeof(T));
            return true;
        }

        void CopyMemTo(std::vector<T>& out) {
            static_assert(std::is_trivially_copyable_v<T>, "CopyMemTo requires trivially copyable T");
            std::lock_guard<std::mutex> guard(mtx_);
            out.resize(inner_.size());
            memcpy(out.data(), inner_.data(), inner_.size() * sizeof(T));
        }

    private:
        std::mutex mtx_;
        std::vector<T> inner_;

    };

}

#endif //TC_APPLICATION_CONCURRENT_VECTOR_H
