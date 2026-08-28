#ifndef PX_COMMON_NEW_ASYNC_OPERATION_H
#define PX_COMMON_NEW_ASYNC_OPERATION_H

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <asio2/external/asio.hpp>

#include "async_result.h"
#include "async_runtime.h"

namespace px {

template<typename T>
class PxAsyncOneShot final {
public:
    static std::shared_ptr<PxAsyncOneShot> Create(asio::any_io_executor executor) {
        return std::make_shared<PxAsyncOneShot>(std::move(executor));
    }

    explicit PxAsyncOneShot(asio::any_io_executor executor)
        : executor_(std::move(executor)) {}

    PxAsyncOneShot(const PxAsyncOneShot&) = delete;
    PxAsyncOneShot& operator=(const PxAsyncOneShot&) = delete;

    [[nodiscard]] bool TryComplete(PxResult<T> result) {
        std::shared_ptr<asio::steady_timer> waiter;
        {
            std::lock_guard lock(mutex_);
            if (result_) {
                return false;
            }
            result_.emplace(std::move(result));
            waiter = waiter_.lock();
        }
        if (waiter) {
            asio::post(executor_, [waiter]() {
                asio::error_code ignored;
                waiter->cancel(ignored);
            });
        }
        return true;
    }

    [[nodiscard]] bool TryFail(PxAsyncError error) {
        return TryComplete(PxResult<T>::Failure(std::move(error)));
    }

    [[nodiscard]] bool IsCompleted() const {
        std::lock_guard lock(mutex_);
        return result_.has_value();
    }

    static PxAwaitable<PxResult<T>> WaitUntil(
        std::shared_ptr<PxAsyncOneShot> operation,
        std::chrono::steady_clock::time_point deadline) {
        const auto cancellation = co_await asio::this_coro::cancellation_state;
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            static_cast<void>(operation->TryFail(MakePxAsyncError(
                PxAsyncErrorCode::kCancelled, "wait", "asynchronous operation was cancelled")));
        }

        {
            std::lock_guard lock(operation->mutex_);
            if (operation->waiting_) {
                co_return PxResult<T>::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kRequestInProgress,
                    "wait",
                    "asynchronous operation already has a waiter"));
            }
            operation->waiting_ = true;
            if (operation->result_) {
                co_return std::move(*operation->result_);
            }
        }

        auto timer = std::make_shared<asio::steady_timer>(operation->executor_);
        timer->expires_at(deadline);
        {
            std::lock_guard lock(operation->mutex_);
            if (operation->result_) {
                co_return std::move(*operation->result_);
            }
            operation->waiter_ = timer;
        }

        asio::error_code wait_error;
        co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));

        const auto after_wait_cancellation = co_await asio::this_coro::cancellation_state;
        if (after_wait_cancellation.cancelled() != asio::cancellation_type::none) {
            static_cast<void>(operation->TryFail(MakePxAsyncError(
                PxAsyncErrorCode::kCancelled, "wait", "asynchronous operation was cancelled")));
        }
        else if (!wait_error) {
            static_cast<void>(operation->TryFail(MakePxAsyncError(
                PxAsyncErrorCode::kTimeout, "wait", "asynchronous operation timed out", true)));
        }

        std::lock_guard lock(operation->mutex_);
        operation->waiter_.reset();
        if (!operation->result_) {
            operation->result_.emplace(PxResult<T>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kProtocolError,
                "wait",
                "asynchronous operation resumed without a result")));
        }
        co_return std::move(*operation->result_);
    }

private:
    asio::any_io_executor executor_;
    mutable std::mutex mutex_;
    std::optional<PxResult<T>> result_;
    std::weak_ptr<asio::steady_timer> waiter_;
    bool waiting_ = false;
};

template<typename T>
class PxAsyncRequestRegistry final {
public:
    using Operation = PxAsyncOneShot<T>;

    explicit PxAsyncRequestRegistry(asio::any_io_executor executor)
        : executor_(std::move(executor)) {}

    PxAsyncRequestRegistry(const PxAsyncRequestRegistry&) = delete;
    PxAsyncRequestRegistry& operator=(const PxAsyncRequestRegistry&) = delete;

    PxResult<std::shared_ptr<Operation>> Register(const std::string& request_id) {
        if (request_id.empty()) {
            return PxResult<std::shared_ptr<Operation>>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kInvalidArgument,
                "register",
                "request id is empty"));
        }
        const auto operation = Operation::Create(executor_);
        {
            std::lock_guard lock(mutex_);
            if (operations_.contains(request_id)) {
                return PxResult<std::shared_ptr<Operation>>::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kRequestInProgress,
                    "register",
                    "a request with the same id is already pending"));
            }
            operations_.emplace(request_id, operation);
        }
        return PxResult<std::shared_ptr<Operation>>::Success(operation);
    }

    [[nodiscard]] bool Complete(const std::string& request_id, PxResult<T> result) {
        std::shared_ptr<Operation> operation;
        {
            std::lock_guard lock(mutex_);
            const auto it = operations_.find(request_id);
            if (it == operations_.end()) {
                return false;
            }
            operation = it->second;
            operations_.erase(it);
        }
        return operation->TryComplete(std::move(result));
    }

    [[nodiscard]] bool RemoveIf(const std::string& request_id,
                                const std::shared_ptr<Operation>& operation) {
        std::lock_guard lock(mutex_);
        const auto it = operations_.find(request_id);
        if (it == operations_.end() || it->second != operation) {
            return false;
        }
        operations_.erase(it);
        return true;
    }

    std::size_t FailAll(const PxAsyncError& error) {
        std::vector<std::shared_ptr<Operation>> operations;
        {
            std::lock_guard lock(mutex_);
            operations.reserve(operations_.size());
            for (auto& [request_id, operation] : operations_) {
                static_cast<void>(request_id);
                operations.push_back(std::move(operation));
            }
            operations_.clear();
        }
        for (const auto& operation : operations) {
            static_cast<void>(operation->TryFail(error));
        }
        return operations.size();
    }

    [[nodiscard]] std::size_t Size() const {
        std::lock_guard lock(mutex_);
        return operations_.size();
    }

private:
    asio::any_io_executor executor_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Operation>> operations_;
};

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_OPERATION_H
