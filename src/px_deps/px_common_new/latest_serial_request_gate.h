#ifndef PX_COMMON_NEW_LATEST_SERIAL_REQUEST_GATE_H
#define PX_COMMON_NEW_LATEST_SERIAL_REQUEST_GATE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace px {

// Serializes remote mutations while allowing a newer request to supersede
// queued work and reject late completion from an older generation.
class LatestSerialRequestGate final {
public:
    struct Request final {
        std::uint64_t generation = 0;
        std::shared_ptr<std::atomic_bool> cancellation;

        explicit operator bool() const noexcept {
            return generation != 0 && cancellation;
        }
    };

    static std::shared_ptr<LatestSerialRequestGate> Create() {
        return std::make_shared<LatestSerialRequestGate>();
    }

    [[nodiscard]] Request Begin() {
        std::lock_guard lock(state_mutex_);
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
        std::lock_guard lock(state_mutex_);
        return IsCurrentLocked(generation);
    }

    [[nodiscard]] bool RunIfCurrent(
        std::uint64_t generation, std::function<void()> operation) {
        std::lock_guard execution_lock(execution_mutex_);
        {
            std::lock_guard state_lock(state_mutex_);
            if (!IsCurrentLocked(generation)) {
                return false;
            }
        }
        operation();
        return true;
    }

    [[nodiscard]] bool Complete(std::uint64_t generation) {
        std::lock_guard lock(state_mutex_);
        if (!IsCurrentLocked(generation)) {
            return false;
        }
        active_ = false;
        cancellation_.reset();
        return true;
    }

    void Stop() {
        std::lock_guard lock(state_mutex_);
        stopped_ = true;
        CancelActive();
        ++generation_;
    }

private:
    [[nodiscard]] bool IsCurrentLocked(std::uint64_t generation) const {
        return !stopped_ && active_ && generation != 0
            && generation == generation_;
    }

    void CancelActive() {
        if (cancellation_) {
            cancellation_->store(true, std::memory_order_release);
        }
        active_ = false;
        cancellation_.reset();
    }

    mutable std::mutex state_mutex_;
    std::mutex execution_mutex_;
    std::uint64_t generation_ = 0;
    bool active_ = false;
    bool stopped_ = false;
    std::shared_ptr<std::atomic_bool> cancellation_;
};

} // namespace px

#endif // PX_COMMON_NEW_LATEST_SERIAL_REQUEST_GATE_H
