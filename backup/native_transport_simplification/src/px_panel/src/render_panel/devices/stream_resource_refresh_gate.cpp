#include "stream_resource_refresh_gate.h"

namespace px {

std::shared_ptr<StreamResourceRefreshGate> StreamResourceRefreshGate::Create() {
    return std::make_shared<StreamResourceRefreshGate>();
}

std::optional<std::uint64_t> StreamResourceRefreshGate::Begin(bool supersede) {
    std::lock_guard lock(mutex_);
    if (stopped_ || (active_ && !supersede)) {
        return std::nullopt;
    }
    active_ = true;
    return ++generation_;
}

bool StreamResourceRefreshGate::Complete(std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    if (stopped_ || !active_ || generation != generation_) {
        return false;
    }
    active_ = false;
    return true;
}

bool StreamResourceRefreshGate::RunIfCurrent(
    std::uint64_t generation, std::function<void()> operation) {
    // Keep superseded identity refreshes ordered. If an old operation has
    // already started, its replacement runs after it and becomes the final DB
    // projection; if it has not started, it is rejected by the generation check.
    std::lock_guard execution_lock(execution_mutex_);
    {
        std::lock_guard state_lock(mutex_);
        if (stopped_ || !active_ || generation != generation_) {
            return false;
        }
    }
    operation();
    return true;
}

void StreamResourceRefreshGate::Stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    active_ = false;
    ++generation_;
}

} // namespace px
