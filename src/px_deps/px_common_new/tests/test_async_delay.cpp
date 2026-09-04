#include <chrono>
#include <future>
#include <memory>

#include <gtest/gtest.h>

#include "px_common_new/async_delay.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> CompleteDelay(std::chrono::steady_clock::duration delay,
                                std::shared_ptr<std::promise<PxResult<void>>> completion) {
    completion->set_value(co_await WaitForAsyncDelay(delay, "delay-test"));
}

TEST(AsyncDelay, CompletesAtDeadline) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(scope->Spawn("short-delay", [completion]() { return CompleteDelay(20ms, completion); }));

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(future.get());
    EXPECT_GE(std::chrono::steady_clock::now() - started, 15ms);
    EXPECT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(AsyncDelay, ScopeCancellationInterruptsTimer) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("cancel-delay", [completion]() { return CompleteDelay(10s, completion); }));

    scope->BeginStop();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    EXPECT_EQ(result.Error().stage, "delay-test");
    EXPECT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(AsyncDelay, RejectsNegativeDuration) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("invalid-delay", [completion]() { return CompleteDelay(-1ms, completion); }));

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kInvalidArgument);
    EXPECT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px
