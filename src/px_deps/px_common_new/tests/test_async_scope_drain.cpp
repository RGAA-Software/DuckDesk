#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/async_delay.h"
#include "px_common_new/async_scope_drain.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> ShortTask() {
    static_cast<void>(co_await WaitForAsyncDelay(20ms, "scope-drain.short-task"));
}

PxAwaitable<void> BlockingTask() {
    std::this_thread::sleep_for(150ms);
    co_return;
}

PxAwaitable<void> CollectDrainResult(
    std::shared_ptr<PxAsyncScope> target,
    std::chrono::steady_clock::time_point deadline,
    std::shared_ptr<std::promise<PxResult<PxAsyncScopeStatistics>>> completion) {
    completion->set_value(co_await WaitForAsyncScopeDrain(std::move(target), deadline, "scope-drain.test"));
}

TEST(AsyncScopeDrain, CompletesWithoutBlockingRuntimeThread) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto target = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
    const auto observer = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto completion = std::make_shared<std::promise<PxResult<PxAsyncScopeStatistics>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(target->Spawn("short-task", [] { return ShortTask(); }));
    ASSERT_TRUE(observer->Spawn("wait-drain", [target, completion] {
        return CollectDrainResult(target, std::chrono::steady_clock::now() + 1s, completion);
    }));

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value().outstanding, 0);
    EXPECT_TRUE(observer->WaitFor(1s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(AsyncScopeDrain, ReportsDeadlineWithOutstandingTask) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto target = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
    const auto observer = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto completion = std::make_shared<std::promise<PxResult<PxAsyncScopeStatistics>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(target->Spawn("blocking-task", [] { return BlockingTask(); }));
    ASSERT_TRUE(observer->Spawn("wait-drain-timeout", [target, completion] {
        return CollectDrainResult(target, std::chrono::steady_clock::now() + 20ms, completion);
    }));

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_EQ(result.Error().stage, "scope-drain.test");
    EXPECT_TRUE(target->WaitFor(1s));
    EXPECT_TRUE(observer->WaitFor(1s));
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px
