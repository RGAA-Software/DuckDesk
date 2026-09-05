#ifndef PX_COMMON_NEW_ASYNC_BLOCKING_CALL_H
#define PX_COMMON_NEW_ASYNC_BLOCKING_CALL_H

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "async_operation.h"

namespace px {

using PxBlockingTaskPoster = std::function<void(std::function<void()>)>;

// Bridges one blocking operation into a cancellable, deadline-bound awaitable.
// The blocking callable receives the same cancellation signal so transports
// such as HttpClient can abort in-flight work rather than merely reject a late
// result.
template <typename T>
PxAwaitable<PxResult<T>> AwaitBlockingCall(PxBlockingTaskPoster post_blocking, asio::any_io_executor completion_executor,
                                           std::chrono::steady_clock::time_point deadline, std::shared_ptr<std::atomic_bool> cancellation,
                                           std::string stage, std::function<T(const std::shared_ptr<std::atomic_bool>&)> call) {
    if (!post_blocking || !call || !cancellation) {
        co_return PxResult<T>::Failure(MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, std::move(stage),
                                                        "blocking operation is missing its executor, callable, or cancellation signal"));
    }
    if (cancellation->load(std::memory_order_acquire)) {
        co_return PxResult<T>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kCancelled, std::move(stage), "blocking operation was cancelled before dispatch"));
    }
    if (std::chrono::steady_clock::now() >= deadline) {
        co_return PxResult<T>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kTimeout, std::move(stage), "blocking operation deadline expired before dispatch", true));
    }

    const auto operation = PxAsyncOneShot<T>::Create(std::move(completion_executor));
    try {
        post_blocking([operation, cancellation, call = std::move(call), stage]() mutable {
            if (cancellation->load(std::memory_order_acquire)) {
                static_cast<void>(
                    operation->TryFail(MakePxAsyncError(PxAsyncErrorCode::kCancelled, stage, "blocking operation was cancelled before execution")));
                return;
            }
            try {
                auto value = call(cancellation);
                if (cancellation->load(std::memory_order_acquire)) {
                    static_cast<void>(operation->TryFail(
                        MakePxAsyncError(PxAsyncErrorCode::kCancelled, stage, "blocking operation was cancelled during execution")));
                    return;
                }
                static_cast<void>(operation->TryComplete(PxResult<T>::Success(std::move(value))));
            } catch (const std::exception& error) {
                static_cast<void>(operation->TryFail(MakePxAsyncError(PxAsyncErrorCode::kProtocolError, stage, error.what(), true)));
            } catch (...) {
                static_cast<void>(operation->TryFail(
                    MakePxAsyncError(PxAsyncErrorCode::kProtocolError, stage, "blocking operation threw a non-standard exception", true)));
            }
        });
    } catch (const std::exception& error) {
        co_return PxResult<T>::Failure(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, std::move(stage), error.what(), true));
    } catch (...) {
        co_return PxResult<T>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, std::move(stage), "blocking executor rejected the operation", true));
    }

    auto result = co_await PxAsyncOneShot<T>::WaitUntil(operation, deadline);
    if (!result) {
        auto error = result.Error();
        if (error.code == PxAsyncErrorCode::kTimeout || error.code == PxAsyncErrorCode::kCancelled) {
            cancellation->store(true, std::memory_order_release);
        }
        if (error.stage == "wait") {
            error.stage = std::move(stage);
        }
        co_return PxResult<T>::Failure(std::move(error));
    }
    if (cancellation->load(std::memory_order_acquire)) {
        co_return PxResult<T>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kCancelled, std::move(stage), "blocking operation was cancelled before completion"));
    }
    co_return result;
}

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_BLOCKING_CALL_H
