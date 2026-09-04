#ifndef PX_COMMON_NEW_ASYNC_MAILBOX_H
#define PX_COMMON_NEW_ASYNC_MAILBOX_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

#include "async_operation.h"
#include "async_result.h"
#include "async_runtime.h"

namespace px {

struct PxAsyncMailboxStatistics final {
    std::uint64_t accepted{0};
    std::uint64_t received{0};
    std::uint64_t rejected_full{0};
    std::uint64_t rejected_closed{0};
    std::uint64_t receive_failures{0};
    std::uint64_t stale_waiters{0};
    std::uint64_t dropped_on_close{0};
    std::size_t depth{0};
    std::size_t high_watermark{0};
};

template <typename T> class PxAsyncMailbox final {
  public:
    static std::shared_ptr<PxAsyncMailbox> Create(asio::any_io_executor executor, std::size_t capacity) {
        if (capacity == 0) {
            return {};
        }
        return std::make_shared<PxAsyncMailbox>(std::move(executor), capacity);
    }

    PxAsyncMailbox(asio::any_io_executor executor, std::size_t capacity) : executor_(std::move(executor)), capacity_(capacity) {}

    PxAsyncMailbox(const PxAsyncMailbox&) = delete;
    PxAsyncMailbox& operator=(const PxAsyncMailbox&) = delete;

    [[nodiscard]] PxResult<void> TryPush(T value) {
        const auto envelope = std::make_shared<T>(std::move(value));
        for (;;) {
            std::shared_ptr<Operation> waiter;
            {
                std::lock_guard lock(mutex_);
                if (closed_) {
                    ++statistics_.rejected_closed;
                    return PxResult<void>::Failure(closed_error_);
                }
                waiter = waiter_;
                if (!waiter) {
                    if (queue_.size() >= capacity_) {
                        ++statistics_.rejected_full;
                        return PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kQueueFull, "mailbox.push", "asynchronous mailbox is full",
                                                                        true, "ASYNC_MAILBOX_FULL"));
                    }
                    queue_.push_back(envelope);
                    ++statistics_.accepted;
                    statistics_.depth = queue_.size();
                    statistics_.high_watermark = std::max(statistics_.high_watermark, statistics_.depth);
                    return PxResult<void>::Success();
                }
            }

            if (waiter->TryComplete(PxResult<Envelope>::Success(envelope))) {
                std::lock_guard lock(mutex_);
                if (waiter_ == waiter) {
                    waiter_.reset();
                }
                ++statistics_.accepted;
                return PxResult<void>::Success();
            }

            std::lock_guard lock(mutex_);
            if (waiter_ == waiter) {
                waiter_.reset();
            }
            ++statistics_.stale_waiters;
        }
    }

    static PxAwaitable<PxResult<T>> ReceiveUntil(std::shared_ptr<PxAsyncMailbox> mailbox, std::chrono::steady_clock::time_point deadline) {
        if (!mailbox) {
            co_return PxResult<T>::Failure(MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "mailbox.receive", "asynchronous mailbox is null"));
        }

        std::shared_ptr<Operation> operation;
        {
            std::lock_guard lock(mailbox->mutex_);
            if (!mailbox->queue_.empty()) {
                auto envelope = std::move(mailbox->queue_.front());
                mailbox->queue_.pop_front();
                ++mailbox->statistics_.received;
                mailbox->statistics_.depth = mailbox->queue_.size();
                co_return PxResult<T>::Success(std::move(*envelope));
            }
            if (mailbox->closed_) {
                ++mailbox->statistics_.receive_failures;
                co_return PxResult<T>::Failure(mailbox->closed_error_);
            }
            if (mailbox->waiter_) {
                ++mailbox->statistics_.receive_failures;
                co_return PxResult<T>::Failure(MakePxAsyncError(PxAsyncErrorCode::kRequestInProgress, "mailbox.receive",
                                                                "asynchronous mailbox already has an active receiver", false,
                                                                "ASYNC_MAILBOX_RECEIVE_IN_PROGRESS"));
            }
            operation = Operation::Create(mailbox->executor_);
            mailbox->waiter_ = operation;
        }

        auto result = co_await Operation::WaitUntil(operation, deadline);
        {
            std::lock_guard lock(mailbox->mutex_);
            if (mailbox->waiter_ == operation) {
                mailbox->waiter_.reset();
            }
            if (result) {
                ++mailbox->statistics_.received;
            } else {
                ++mailbox->statistics_.receive_failures;
            }
        }

        if (!result) {
            co_return PxResult<T>::Failure(result.Error());
        }
        auto envelope = result.TakeValue();
        if (!envelope) {
            co_return PxResult<T>::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "mailbox.receive", "asynchronous mailbox returned an empty value"));
        }
        co_return PxResult<T>::Success(std::move(*envelope));
    }

    [[nodiscard]] bool Close(PxAsyncError reason) {
        std::shared_ptr<Operation> waiter;
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                return false;
            }
            closed_ = true;
            closed_error_ = std::move(reason);
            waiter = std::move(waiter_);
            statistics_.dropped_on_close += queue_.size();
            queue_.clear();
            statistics_.depth = 0;
        }
        if (waiter) {
            static_cast<void>(waiter->TryFail(closed_error_));
        }
        return true;
    }

    [[nodiscard]] bool IsClosed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    [[nodiscard]] PxAsyncMailboxStatistics Statistics() const {
        std::lock_guard lock(mutex_);
        return statistics_;
    }

  private:
    using Envelope = std::shared_ptr<T>;
    using Operation = PxAsyncOneShot<Envelope>;

    asio::any_io_executor executor_;
    const std::size_t capacity_{0};
    mutable std::mutex mutex_;
    std::deque<Envelope> queue_;
    std::shared_ptr<Operation> waiter_;
    PxAsyncError closed_error_{MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "mailbox.close", "asynchronous mailbox is closed")};
    PxAsyncMailboxStatistics statistics_{};
    bool closed_{false};
};

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_MAILBOX_H
