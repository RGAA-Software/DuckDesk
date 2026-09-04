#pragma once

#include <chrono>
#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "px_common_new/async_operation.h"
#include "px_common_new/async_result.h"
#include "px_common_new/async_runtime.h"

namespace px {

// Adapts a legacy callback that reports one copyable typed value. A callback
// arriving after timeout/cancellation is routed to late_completion so callers
// can compensate an already committed side effect (for example, close a
// logical-session binding).
//
// Lifetime:
// - The coroutine owns the one-shot operation until completion.
// - The external callback captures only a weak operation.
// - late_completion must capture owners weakly.
//
// Threading:
// - Completion may arrive from any thread.
// - PxAsyncOneShot resumes the waiter on its executor.
// - No lock is held while starter or late_completion runs.
template<std::copy_constructible T, typename Starter, typename LateCompletion>
PxAwaitable<PxResult<T>> AwaitWsValueCallback(
    Starter starter,
    const std::chrono::steady_clock::time_point deadline,
    std::string operation_name,
    LateCompletion late_completion) {
    const auto executor = co_await asio::this_coro::executor;
    const auto operation = PxAsyncOneShot<T>::Create(executor);
    const std::weak_ptr<PxAsyncOneShot<T>> weak_operation = operation;
    try {
        const bool started = starter(
            [weak_operation,
             late_completion = std::move(late_completion)](T value) {
                const auto active_operation = weak_operation.lock();
                if (active_operation && active_operation->TryComplete(
                        PxResult<T>::Success(value))) {
                    return;
                }
                late_completion(value);
            });
        if (!started) {
            static_cast<void>(operation->TryFail(MakePxAsyncError(
                PxAsyncErrorCode::kServiceRejected,
                operation_name,
                "callback operation was rejected before start",
                false,
                "CALLBACK_START_REJECTED")));
        }
    }
    catch (const std::exception& error) {
        static_cast<void>(operation->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kProtocolError,
            operation_name,
            error.what(),
            false,
            "CALLBACK_START_EXCEPTION")));
    }
    catch (...) {
        static_cast<void>(operation->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kProtocolError,
            operation_name,
            "callback starter raised an unknown exception",
            false,
            "CALLBACK_START_EXCEPTION")));
    }
    co_return co_await PxAsyncOneShot<T>::WaitUntil(operation, deadline);
}

}  // namespace px
