#include "reconnect_backoff.h"

#include <limits>

#include "async_delay.h"

namespace px {
std::shared_ptr<PxReconnectBackoff> PxReconnectBackoff::Create(PxReconnectBackoffOptions options) {
    if (options.retry_delay < std::chrono::milliseconds::zero()) {
        return {};
    }
    return std::make_shared<PxReconnectBackoff>(std::move(options));
}

PxReconnectBackoff::PxReconnectBackoff(PxReconnectBackoffOptions options) : options_(std::move(options)) {}

PxReconnectBackoffStep PxReconnectBackoff::Next() {
    std::lock_guard lock(mutex_);
    if (attempt_count_ < std::numeric_limits<std::uint32_t>::max()) {
        ++attempt_count_;
    }
    return PxReconnectBackoffStep{.attempt = attempt_count_, .nominal_delay = options_.retry_delay, .delay = options_.retry_delay};
}

void PxReconnectBackoff::Reset() {
    std::lock_guard lock(mutex_);
    attempt_count_ = 0;
}

std::uint32_t PxReconnectBackoff::AttemptCount() const {
    std::lock_guard lock(mutex_);
    return attempt_count_;
}

PxAwaitable<PxResult<void>> PxReconnectBackoff::Wait(std::chrono::milliseconds delay) {
    if (delay < std::chrono::milliseconds::zero()) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "reconnect.wait", "reconnect delay must not be negative"));
    }

    co_return co_await WaitForAsyncDelay(delay, "reconnect.wait");
}

} // namespace px
