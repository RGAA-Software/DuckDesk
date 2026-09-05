#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

#include "px_common/async_runtime.h"
#include "px_common/async_operation.h"

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

PxAwaitable<void> AwaitIntOperation(
    std::shared_ptr<PxAsyncOneShot<int>> operation,
    std::chrono::steady_clock::time_point deadline,
    std::shared_ptr<std::promise<PxResult<int>>> completion) {
    completion->set_value(co_await PxAsyncOneShot<int>::WaitUntil(operation, deadline));
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

TEST(PxAsyncOperation, CompletesExactlyOnceAndRejectsLateCompletion) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto operation = PxAsyncOneShot<int>::Create(scope->Executor());
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("one-shot", [operation, completion]() {
        return AwaitIntOperation(operation, std::chrono::steady_clock::now() + 2s,
                                 completion);
    }));
    ASSERT_TRUE(operation->TryComplete(PxResult<int>::Success(42)));
    EXPECT_FALSE(operation->TryComplete(PxResult<int>::Success(99)));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 42);
    ASSERT_TRUE(scope->WaitFor(2s));

    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncResult, VoidAndStableErrorCodeAreExplicit) {
    const auto success = PxResult<void>::Success();
    EXPECT_TRUE(success.HasValue());

    const auto failure = PxResult<void>::Failure(MakePxAsyncError(
        PxAsyncErrorCode::kServiceRejected,
        "ticket",
        "ticket rejected",
        false,
        "AUTHORIZATION_INVALID"));
    ASSERT_FALSE(failure.HasValue());
    EXPECT_EQ(failure.Error().code, PxAsyncErrorCode::kServiceRejected);
    EXPECT_EQ(failure.Error().StableCode(), "AUTHORIZATION_INVALID");
}

TEST(PxAsyncOperation, DeadlineReturnsTypedTimeout) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime);
    const auto operation = PxAsyncOneShot<int>::Create(scope->Executor());
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("timeout", [operation, completion]() {
        return AwaitIntOperation(operation, std::chrono::steady_clock::now() + 25ms,
                                 completion);
    }));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    ASSERT_TRUE(scope->WaitFor(2s));

    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncOperation, ScopeCancellationReturnsTypedCancellation) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime);
    const auto operation = PxAsyncOneShot<int>::Create(scope->Executor());
    const auto completion = std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("cancel-one-shot", [operation, completion]() {
        return AwaitIntOperation(operation, std::chrono::steady_clock::now() + 1h,
                                 completion);
    }));
    scope->BeginStop();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    EXPECT_FALSE(operation->TryComplete(PxResult<int>::Success(7)));
    ASSERT_TRUE(scope->WaitFor(2s));

    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncRequestRegistry, DuplicateIdentityRemovalAndFailAllAreSafe) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto registry = std::make_shared<PxAsyncRequestRegistry<int>>(
        runtime->Executor(PxAsyncLane::kState));

    auto first = registry->Register("request-1");
    ASSERT_TRUE(first.HasValue());
    const auto operation = first.Value();
    const auto duplicate = registry->Register("request-1");
    ASSERT_FALSE(duplicate.HasValue());
    EXPECT_EQ(duplicate.Error().code, PxAsyncErrorCode::kRequestInProgress);
    EXPECT_FALSE(registry->RemoveIf("request-1", PxAsyncOneShot<int>::Create(
        runtime->Executor(PxAsyncLane::kState))));
    EXPECT_EQ(registry->Size(), 1U);

    EXPECT_EQ(registry->FailAll(MakePxAsyncError(
        PxAsyncErrorCode::kServiceStopped, "test", "stopped")), 1U);
    EXPECT_EQ(registry->Size(), 0U);
    EXPECT_TRUE(operation->IsCompleted());
    EXPECT_FALSE(registry->Complete("request-1", PxResult<int>::Success(1)));

    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncOperation, CompletionCancellationRaceIsStableForTenRounds) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = PxAsyncRuntime::Create();
        ASSERT_TRUE(runtime->Start());
        const auto scope = PxAsyncScope::Create(runtime);
        const auto operation = PxAsyncOneShot<int>::Create(scope->Executor());
        const auto completion = std::make_shared<std::promise<PxResult<int>>>();
        auto future = completion->get_future();

        ASSERT_TRUE(scope->Spawn("race", [operation, completion]() {
            return AwaitIntOperation(operation, std::chrono::steady_clock::now() + 2s,
                                     completion);
        }));
        asio::post(runtime->Executor(PxAsyncLane::kWorker), [operation, round]() {
            static_cast<void>(operation->TryComplete(PxResult<int>::Success(round)));
        });
        scope->BeginStop();

        ASSERT_EQ(future.wait_for(2s), std::future_status::ready) << "round=" << round;
        const auto result = future.get();
        if (result.HasValue()) {
            EXPECT_EQ(result.Value(), round);
        }
        else {
            EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
        }
        ASSERT_TRUE(scope->WaitFor(2s));
        runtime->RequestStop();
        runtime->Join();
    }
}

} // namespace
} // namespace px
