#ifndef PX_COMMON_NEW_LATEST_ASYNC_GENERATION_H
#define PX_COMMON_NEW_LATEST_ASYNC_GENERATION_H

#include <cstdint>
#include <memory>
#include <mutex>

namespace px
{
    // Makes only the latest asynchronous request eligible to update its owner.
    // Stop permanently rejects queued completions during owner destruction.
    class LatestAsyncGeneration final {
    public:
        static std::shared_ptr<LatestAsyncGeneration> Create() {
            return std::make_shared<LatestAsyncGeneration>();
        }

        [[nodiscard]] std::uint64_t Begin() {
            std::lock_guard lock(mutex_);
            if (stopped_) return 0;
            active_ = true;
            return ++generation_;
        }

        [[nodiscard]] bool Complete(std::uint64_t generation) {
            std::lock_guard lock(mutex_);
            if (stopped_ || !active_ || generation == 0
                || generation != generation_) return false;
            active_ = false;
            return true;
        }

        void Stop() {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            active_ = false;
            ++generation_;
        }

    private:
        mutable std::mutex mutex_;
        std::uint64_t generation_ = 0;
        bool active_ = false;
        bool stopped_ = false;
    };
}

#endif // PX_COMMON_NEW_LATEST_ASYNC_GENERATION_H
