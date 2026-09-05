#ifndef PX_COMMON_NEW_ASYNC_SCOPE_DRAIN_H
#define PX_COMMON_NEW_ASYNC_SCOPE_DRAIN_H

#include <chrono>
#include <memory>
#include <string>

#include "async_result.h"
#include "async_runtime.h"

namespace px {

[[nodiscard]] PxAwaitable<PxResult<PxAsyncScopeStatistics>> WaitForAsyncScopeDrain(
    std::shared_ptr<PxAsyncScope> scope,
    std::chrono::steady_clock::time_point deadline,
    std::string stage);

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_SCOPE_DRAIN_H
