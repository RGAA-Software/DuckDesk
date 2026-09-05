#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

#include "px_common_new/async_blocking_call.h"
#include "px_common_new/async_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxBlockingTaskPoster MakeBlockingPoster(const std::shared_ptr<PxAsyncRuntime>& runtime) {
    return [runtime](std::function<void()> task) {
        if (!runtime->DeferBlocking(std::move(task))) {
            throw std::runtime_error("blocking executor rejected test task");
        }
    };
}

template <typename T>
PxAwaitable<void> AwaitResult(PxBlockingTaskPoster poster, std::shared_ptr<std::atomic_bool> cancellation,
                              std::chrono::steady_clock::time_point deadline, std::function<T(const std::shared_ptr<std::atomic_bool>&)> call,
                              std::shared_ptr<std::promise<PxResult<T>>> completion) {
    const auto executor = co_await asio::this_coro::executor;
    completion->set_value(co_await AwaitBlockingCall<T>(poster, executor, deadline, cancellation, "blocking-test", std::move(call)));
}

TEST(AsyncBlockingCall, CompletesWithTypedValue) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto poster = MakeBlockingPoster(runtime);
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("blocking-value", [=]() {
        return AwaitResult<int>(
            poster, cancellation, std::chrono::steady_clock::now() + 2s, [](const auto&) { return 42; }, completion);
    }));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value(), 42);
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(AsyncBlockingCall, CancellationDuringExecutionRejectsLateValue) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto poster = MakeBlockingPoster(runtime);
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    const auto entered = std::make_shared<std::promise<void>>();
    const auto release = std::make_shared<std::promise<void>>();
    const auto release_future = release->get_future().share();
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("blocking-cancel", [=]() {
        return AwaitResult<int>(
            poster, cancellation, std::chrono::steady_clock::now() + 2s,
            [entered, release_future](const auto&) {
                entered->set_value();
                release_future.wait();
                return 7;
            },
            completion);
    }));
    entered->get_future().wait();
    cancellation->store(true, std::memory_order_release);
    release->set_value();

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    EXPECT_EQ(result.Error().stage, "blocking-test");
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(AsyncBlockingCall, DeadlineReturnsTypedTimeout) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto poster = MakeBlockingPoster(runtime);
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    const auto release = std::make_shared<std::promise<void>>();
    const auto release_future = release->get_future().share();
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("blocking-timeout", [=]() {
        return AwaitResult<int>(
            poster, cancellation, std::chrono::steady_clock::now() + 25ms,
            [release_future](const auto&) {
                release_future.wait();
                return 1;
            },
            completion);
    }));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_EQ(result.Error().stage, "blocking-test");
    EXPECT_TRUE(cancellation->load(std::memory_order_acquire));
    release->set_value();
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px
