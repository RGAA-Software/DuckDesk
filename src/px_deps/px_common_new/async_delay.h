#ifndef PX_COMMON_NEW_ASYNC_DELAY_H
#define PX_COMMON_NEW_ASYNC_DELAY_H

#include <chrono>
#include <string>

#include "async_result.h"
#include "async_runtime.h"

namespace px {

[[nodiscard]] PxAwaitable<PxResult<void>> WaitForAsyncDelay(std::chrono::steady_clock::duration delay, std::string stage);

[[nodiscard]] PxAwaitable<PxResult<void>> WaitForAsyncDeadline(std::chrono::steady_clock::time_point deadline, std::string stage);

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_DELAY_H
