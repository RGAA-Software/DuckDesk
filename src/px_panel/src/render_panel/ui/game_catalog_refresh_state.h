#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace px {

    class GameCatalogRefreshState final {
    public:
        struct Request final {
            std::uint64_t generation = 0;
            std::shared_ptr<std::atomic_bool> cancellation;
        };

        [[nodiscard]] Request Begin() {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return {};
            }
            CancelActive();
            active_ = true;
            ++generation_;
            cancellation_ = std::make_shared<std::atomic_bool>(false);
            return {generation_, cancellation_};
        }

        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const {
            std::lock_guard lock(mutex_);
            return !stopped_ && active_ && generation != 0
                && generation == generation_;
        }

        [[nodiscard]] bool Complete(std::uint64_t generation) {
            std::lock_guard lock(mutex_);
            if (stopped_ || !active_ || generation == 0
                || generation != generation_) {
                return false;
            }
            active_ = false;
            cancellation_.reset();
            return true;
        }

        void Stop() {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            CancelActive();
            ++generation_;
        }

    private:
        void CancelActive() {
            if (cancellation_) {
                cancellation_->store(true, std::memory_order_release);
            }
            active_ = false;
            cancellation_.reset();
        }

        mutable std::mutex mutex_;
        std::uint64_t generation_ = 0;
        bool active_ = false;
        bool stopped_ = false;
        std::shared_ptr<std::atomic_bool> cancellation_;
    };

}
