#include "callback_quiescence.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "async_delay.h"

namespace px {

class PxCallbackQuiescence::State final {
public:
    mutable std::mutex mutex;
    bool accepting{true};
    std::uint64_t outstanding{0};
};

PxCallbackQuiescence::Lease::Lease(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

PxCallbackQuiescence::Lease::~Lease() {
    Release();
}

PxCallbackQuiescence::Lease::Lease(Lease&& other) noexcept
    : state_(std::move(other.state_)) {}

void PxCallbackQuiescence::Lease::Release() noexcept {
    if (!state_) {
        return;
    }
    {
        std::lock_guard lock(state_->mutex);
        if (state_->outstanding > 0) {
            --state_->outstanding;
        }
    }
    state_.reset();
}

std::shared_ptr<PxCallbackQuiescence> PxCallbackQuiescence::Create() {
    return std::make_shared<PxCallbackQuiescence>();
}

PxCallbackQuiescence::PxCallbackQuiescence()
    : state_(std::make_shared<State>()) {}

std::optional<PxCallbackQuiescence::Lease> PxCallbackQuiescence::TryEnter() {
    std::lock_guard lock(state_->mutex);
    if (!state_->accepting) {
        return std::nullopt;
    }
    ++state_->outstanding;
    return Lease(state_);
}

void PxCallbackQuiescence::BeginStop() {
    std::lock_guard lock(state_->mutex);
    state_->accepting = false;
}

bool PxCallbackQuiescence::IsAccepting() const {
    std::lock_guard lock(state_->mutex);
    return state_->accepting;
}

std::uint64_t PxCallbackQuiescence::Outstanding() const {
    std::lock_guard lock(state_->mutex);
    return state_->outstanding;
}

PxAwaitable<PxResult<void>> PxCallbackQuiescence::WaitUntilQuiescent(
    const std::shared_ptr<PxCallbackQuiescence>& gate,
    const std::chrono::steady_clock::time_point deadline,
    std::string stage) {
    if (!gate) {
        co_return PxResult<void>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kInvalidArgument, std::move(stage), "callback quiescence gate is missing"));
    }
    constexpr auto kPollInterval = std::chrono::milliseconds(2);
    while (gate->Outstanding() != 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kTimeout, std::move(stage), "callback quiescence deadline expired", false));
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto waited = co_await WaitForAsyncDelay(std::min(kPollInterval, remaining), stage);
        if (!waited) {
            co_return waited;
        }
    }
    co_return PxResult<void>::Success();
}

} // namespace px
