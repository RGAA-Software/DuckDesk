#include "async_delay.h"

#include <asio2/external/asio.hpp>

namespace px {

PxAwaitable<PxResult<void>> WaitForAsyncDelay(std::chrono::steady_clock::duration delay, std::string stage) {
    if (delay < std::chrono::steady_clock::duration::zero()) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, std::move(stage), "asynchronous delay must not be negative"));
    }
    co_return co_await WaitForAsyncDeadline(std::chrono::steady_clock::now() + delay, std::move(stage));
}

PxAwaitable<PxResult<void>> WaitForAsyncDeadline(std::chrono::steady_clock::time_point deadline, std::string stage) {
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, deadline);
    asio::error_code wait_error;
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    if (cancellation.cancelled() != asio::cancellation_type::none || wait_error == asio::error::operation_aborted) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kCancelled, std::move(stage), "asynchronous delay was cancelled"));
    }
    if (wait_error) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kProtocolError, std::move(stage), wait_error.message(), true));
    }
    co_return PxResult<void>::Success();
}

} // namespace px
