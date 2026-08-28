#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

#include "px_common_new/async_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

struct LifetimeProbe {
    std::atomic_int resumed{0};
};

PxAwaitable<void> CompleteImmediately(std::shared_ptr<LifetimeProbe> probe) {
    ++probe->resumed;
    co_return;
}

PxAwaitable<void> WaitUntilCancelled(std::shared_ptr<LifetimeProbe> probe) {
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor);
    timer.expires_after(1h);
    co_await timer.async_wait(asio::use_awaitable);
    ++probe->resumed;
    co_return;
}

TEST(PxAsyncRuntime, ScopeCompletesAndReportsStatistics) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime);
    const auto probe = std::make_shared<LifetimeProbe>();

    ASSERT_TRUE(scope->Spawn("complete", [probe]() {
        return CompleteImmediately(probe);
    }));
    ASSERT_TRUE(scope->WaitFor(2s));

    const auto statistics = scope->GetStatistics();
    EXPECT_EQ(probe->resumed.load(), 1);
    EXPECT_EQ(statistics.spawned, 1U);
    EXPECT_EQ(statistics.completed, 1U);
    EXPECT_EQ(statistics.outstanding, 0U);

    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncRuntime, StopCancelsSuspendedCoroutineAndRejectsNewTasks) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime);
    const auto probe = std::make_shared<LifetimeProbe>();

    ASSERT_TRUE(scope->Spawn("cancel-me", [probe]() {
        return WaitUntilCancelled(probe);
    }));
    ASSERT_TRUE(scope->StopAndWait(2s));
    EXPECT_FALSE(scope->Spawn("rejected", [probe]() {
        return CompleteImmediately(probe);
    }));
    EXPECT_EQ(scope->GetStatistics().outstanding, 0U);
    EXPECT_GE(scope->GetStatistics().failed, 1U);

    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncRuntime, QueuedCoroutineReleasesCapturedLifetimeBeforeScopeReturns) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime);
    auto probe = std::make_shared<LifetimeProbe>();
    const std::weak_ptr<LifetimeProbe> weak_probe = probe;

    ASSERT_TRUE(scope->Spawn("lifetime", [probe]() {
        return WaitUntilCancelled(probe);
    }));
    probe.reset();
    ASSERT_TRUE(scope->StopAndWait(2s));
    EXPECT_TRUE(weak_probe.expired());

    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncRuntime, StopMayBeRequestedFromRuntimeCallback) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    auto stopped = std::make_shared<std::promise<void>>();
    auto stopped_future = stopped->get_future();

    asio::post(runtime->Executor(PxAsyncLane::kControl), [runtime, stopped]() {
        runtime->RequestStop();
        stopped->set_value();
    });

    ASSERT_EQ(stopped_future.wait_for(2s), std::future_status::ready);
    runtime->Join();
    EXPECT_TRUE(runtime->IsStopping());
}

TEST(PxAsyncRuntime, RepeatedStartStopTenRounds) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
        ASSERT_TRUE(runtime->Start());
        const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
        const auto probe = std::make_shared<LifetimeProbe>();
        ASSERT_TRUE(scope->Spawn("round", [probe]() {
            return CompleteImmediately(probe);
        }));
        ASSERT_TRUE(scope->WaitFor(2s));
        EXPECT_EQ(probe->resumed.load(), 1) << "round=" << round;
        runtime->RequestStop();
        runtime->Join();
    }
}

} // namespace
} // namespace px
