#include <chrono>
#include <future>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "px_common_new/callback_quiescence.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> CollectQuiescence(std::shared_ptr<PxCallbackQuiescence> gate, const std::chrono::steady_clock::time_point deadline,
                                    std::shared_ptr<std::promise<PxResult<void>>> completion) {
    completion->set_value(co_await PxCallbackQuiescence::WaitUntilQuiescent(gate, deadline, "callback.test"));
}

TEST(CallbackQuiescence, WaitsForOutstandingLeaseAndRejectsNewCallbacks) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto gate = PxCallbackQuiescence::Create();
    auto lease = gate->TryEnter();
    ASSERT_TRUE(lease);
    gate->BeginStop();
    EXPECT_FALSE(gate->TryEnter());

    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("callback-quiescence",
                             [gate, completion] { return CollectQuiescence(gate, std::chrono::steady_clock::now() + 1s, completion); }));
    EXPECT_EQ(future.wait_for(20ms), std::future_status::timeout);
    lease.reset();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(future.get());

    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

TEST(CallbackQuiescence, DeadlineReturnsStableTimeoutWithoutReleasingLease) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto gate = PxCallbackQuiescence::Create();
    auto lease = gate->TryEnter();
    ASSERT_TRUE(lease);
    gate->BeginStop();

    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("callback-timeout",
                             [gate, completion] { return CollectQuiescence(gate, std::chrono::steady_clock::now() + 20ms, completion); }));
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_FALSE(result.Error().retryable);
    EXPECT_EQ(gate->Outstanding(), 1U);
    lease.reset();

    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

} // namespace
} // namespace px
