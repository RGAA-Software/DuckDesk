#include <chrono>
#include <future>
#include <memory>

#include <gtest/gtest.h>

#include "px_common/reconnect_backoff.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> WaitForBackoff(std::chrono::milliseconds delay, std::shared_ptr<std::promise<PxResult<void>>> completion) {
    completion->set_value(co_await PxReconnectBackoff::Wait(delay));
}

TEST(ReconnectBackoff, RejectsInvalidOptions) {
    EXPECT_FALSE(PxReconnectBackoff::Create({.retry_delay = -1ms}));
}

TEST(ReconnectBackoff, RetryDelayIsFixedAndResettable) {
    const auto backoff = PxReconnectBackoff::Create({.retry_delay = 100ms});
    ASSERT_TRUE(backoff);

    EXPECT_EQ(backoff->Next().delay, 100ms);
    EXPECT_EQ(backoff->Next().delay, 100ms);
    EXPECT_EQ(backoff->Next().delay, 100ms);
    EXPECT_EQ(backoff->Next().delay, 100ms);
    EXPECT_EQ(backoff->Next().delay, 100ms);
    EXPECT_EQ(backoff->AttemptCount(), 5U);

    backoff->Reset();
    EXPECT_EQ(backoff->AttemptCount(), 0U);
    EXPECT_EQ(backoff->Next().delay, 100ms);
}

TEST(ReconnectBackoff, RetryDelayNeverGrowsAfterManyFailures) {
    const auto backoff = PxReconnectBackoff::Create({.retry_delay = 1000ms});
    ASSERT_TRUE(backoff);

    for (int attempt{}; attempt != 1000; ++attempt) {
        EXPECT_EQ(backoff->Next().delay, 1000ms);
    }
}

TEST(ReconnectBackoff, ScopeCancellationInterruptsWait) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("cancelled-backoff", [completion]() { return WaitForBackoff(10s, completion); }));

    scope->BeginStop();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    EXPECT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px
