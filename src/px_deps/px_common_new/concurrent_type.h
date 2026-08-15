//
// Created by RGAA on 6/07/2025.
//

#ifndef GAMMARAY_CONCURRENT_TYPE_H
#define GAMMARAY_CONCURRENT_TYPE_H

#include <mutex>
#include <string>
#include <functional>

namespace tc
{
    template<typename T>
    class ConcurrentType {
    public:
        ConcurrentType() {

        }

        ConcurrentType(const ConcurrentType& other) {
            T other_value = other.Clone();
            this->Update(other_value);
        }

        void Update(const T& s) {
            std::lock_guard<std::mutex> guard(mtx_);
            inner_ = s;
        }

        T Clone() const {
            std::lock_guard<std::mutex> guard(mtx_);
            return inner_;
        }

        bool HasValue() const {
            std::lock_guard<std::mutex> guard(mtx_);
            return inner_ ? true : false;
        }

        /**
         *  auto r = c_auther.WithLock([=, this](auto& inner) {
         *       auto c = inner.find_one(make_document(
         *           kvp(kParamAutherName, auther_name)
         *       ));
         *       return c;
         *   });
         * @tparam F
         * @param func
         * @return
         */
        template<typename F>
        decltype(auto) WithLock(F&& func) {
            std::lock_guard<std::mutex> guard(mtx_);
            return func(inner_);
        }

        ConcurrentType<T>& operator=(const ConcurrentType<T>& other) {
            if (this != &other) {
                T other_value = other.Clone();
                this->Update(other_value);
            }
            return *this;
        }

        ConcurrentType<T>& operator=(const T& other) {
            this->Update(other);
            return *this;
        }

        bool operator == (const T& other) {
            return this->Clone() == other;
        }

        bool operator != (const T& other) {
            return this->Clone() != other;
        }

    private:
        mutable std::mutex mtx_;
        T inner_;
    };

    using ConcurrentString = tc::ConcurrentType<std::string>;

    template<typename T>
    using Mutex = tc::ConcurrentType<T>;
}

#endif //GAMMARAY_CONCURRENT_TYPE_H
