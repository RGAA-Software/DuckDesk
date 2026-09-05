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
    EXPECT_FALSE(PxReconnectBackoff::Create({.initial_delay = 2s, .maximum_delay = 1s}));
    EXPECT_FALSE(PxReconnectBackoff::Create({.multiplier = 0.5}));
    EXPECT_FALSE(PxReconnectBackoff::Create({.jitter_ratio = 1.1}));
}

TEST(ReconnectBackoff, ExponentialDelayIsBoundedAndResettable) {
    const auto backoff = PxReconnectBackoff::Create(
        {.initial_delay = 100ms, .maximum_delay = 500ms, .multiplier = 2.0, .jitter_ratio = 0.0, .random_seed = 7});
    ASSERT_TRUE(backoff);

    EXPECT_EQ(backoff->Next().delay, 100ms);
    EXPECT_EQ(backoff->Next().delay, 200ms);
    EXPECT_EQ(backoff->Next().delay, 400ms);
    EXPECT_EQ(backoff->Next().delay, 500ms);
    EXPECT_EQ(backoff->Next().delay, 500ms);
    EXPECT_EQ(backoff->AttemptCount(), 5U);

    backoff->Reset();
    EXPECT_EQ(backoff->AttemptCount(), 0U);
    EXPECT_EQ(backoff->Next().delay, 100ms);
}

TEST(ReconnectBackoff, DeterministicJitterStaysInsideConfiguredWindow) {
    const auto backoff = PxReconnectBackoff::Create(
        {.initial_delay = 1000ms, .maximum_delay = 10s, .multiplier = 2.0, .jitter_ratio = 0.2, .random_seed = 42});
    ASSERT_TRUE(backoff);

    const auto first = backoff->Next();
    const auto second = backoff->Next();
    EXPECT_GE(first.delay, 800ms);
    EXPECT_LE(first.delay, 1200ms);
    EXPECT_GE(second.delay, 1600ms);
    EXPECT_LE(second.delay, 2400ms);
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
