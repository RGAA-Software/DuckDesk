#include "async_scope_drain.h"

#include <algorithm>

#include "async_delay.h"

namespace px {
namespace {

constexpr auto kDrainPollInterval = std::chrono::milliseconds(5);

} // namespace

PxAwaitable<PxResult<PxAsyncScopeStatistics>> WaitForAsyncScopeDrain(
    std::shared_ptr<PxAsyncScope> scope,
    const std::chrono::steady_clock::time_point deadline,
    std::string stage) {
    if (!scope) {
        co_return PxResult<PxAsyncScopeStatistics>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, std::move(stage), "async scope is missing"));
    }

    for (;;) {
        const auto statistics = scope->GetStatistics();
        if (statistics.outstanding == 0) {
            co_return PxResult<PxAsyncScopeStatistics>::Success(statistics);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return PxResult<PxAsyncScopeStatistics>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kTimeout,
                std::move(stage),
                "async scope drain deadline expired with " + std::to_string(statistics.outstanding) + " outstanding task(s)",
                true));
        }

        const auto delay = std::min(kDrainPollInterval, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
        const auto waited = co_await WaitForAsyncDelay(delay, stage);
        if (!waited) {
            co_return PxResult<PxAsyncScopeStatistics>::Failure(waited.Error());
        }
    }
}

} // namespace px
