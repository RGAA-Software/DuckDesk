#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "px_common_new/async_operation.h"
#include "px_common_new/async_result.h"
#include "px_common_new/async_runtime.h"

namespace px::render {

template<typename T>
using OwnedCallbackCompletion = std::function<void(PxResult<T>)>;

// Converts one established callback operation into a cancellable, deadline-bound
// coroutine wait. The callback owns only a weak operation state, so a late ABI
// callback after timeout or owner shutdown cannot retain or access the caller.
template<typename T, typename Starter>
PxAwaitable<PxResult<T>> AwaitOwnedCallback(
    Starter starter,
    const std::chrono::steady_clock::time_point deadline,
    std::string operation_name) {
    const auto executor = co_await asio::this_coro::executor;
    const auto operation = PxAsyncOneShot<T>::Create(executor);
    const std::weak_ptr<PxAsyncOneShot<T>> weak_operation = operation;

    bool started = false;
    try {
        started = starter(
            [weak_operation](PxResult<T> result) {
                if (const auto active_operation = weak_operation.lock()) {
                    static_cast<void>(
                        active_operation->TryComplete(std::move(result)));
                }
            });
    }
    catch (const std::exception& error) {
        static_cast<void>(operation->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kProtocolError,
            std::move(operation_name),
            error.what(),
            false,
            "CALLBACK_START_EXCEPTION")));
    }
    catch (...) {
        static_cast<void>(operation->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kProtocolError,
            std::move(operation_name),
            "callback starter raised an unknown exception",
            false,
            "CALLBACK_START_EXCEPTION")));
    }

    if (!started && !operation->IsCompleted()) {
        static_cast<void>(operation->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kServiceRejected,
            std::move(operation_name),
            "callback operation was rejected before start",
            false,
            "CALLBACK_START_REJECTED")));
    }
    co_return co_await PxAsyncOneShot<T>::WaitUntil(operation, deadline);
}

}  // namespace px::render
