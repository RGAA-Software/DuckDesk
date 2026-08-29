#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/connection_attempt_workflow.h"

namespace px {
namespace {

using namespace std::chrono_literals;

template<typename T>
T Wait(std::future<T>& future) {
    EXPECT_EQ(future.wait_for(3s), std::future_status::ready);
    return future.get();
}

TEST(ConnectionAttemptWorkflow, ReadyCompletesExactlyOnce) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    ASSERT_TRUE(workflow);

    auto promise = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) {
        promise->set_value(std::move(result));
    }));
    EXPECT_TRUE(workflow->MarkReady());
    EXPECT_FALSE(workflow->MarkReady());

    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value().generation, 1);
    EXPECT_TRUE(workflow->IsReady());
    workflow->Stop();
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, FailureHasOneTypedTerminalResult) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    auto promise = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) {
        promise->set_value(std::move(result));
    }));
    EXPECT_TRUE(workflow->FailActive(MakePxAsyncError(
        PxAsyncErrorCode::kProtocolError, "upgrade", "upgrade rejected", true)));
    EXPECT_FALSE(workflow->FailActive(MakePxAsyncError(
        PxAsyncErrorCode::kServiceNotConnected, "disconnect", "late disconnect")));

    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().stage, "upgrade");
    EXPECT_FALSE(workflow->IsReady());
    workflow->Stop();
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, NewGenerationCancelsOlderAttempt) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    auto firstPromise = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto secondPromise = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto first = firstPromise->get_future();
    auto second = secondPromise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([firstPromise](PxConnectionAttemptResult result) {
        firstPromise->set_value(std::move(result));
    }));
    ASSERT_TRUE(workflow->BeginAttempt([secondPromise](PxConnectionAttemptResult result) {
        secondPromise->set_value(std::move(result));
    }));
    EXPECT_TRUE(workflow->MarkReady());

    auto firstResult = Wait(first);
    auto secondResult = Wait(second);
    ASSERT_FALSE(firstResult);
    EXPECT_EQ(firstResult.Error().StableCode(), "CONNECTION_ATTEMPT_REPLACED");
    ASSERT_TRUE(secondResult);
    EXPECT_EQ(secondResult.Value().generation, 2);
    workflow->Stop();
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, TimeoutIsRetryable) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 20ms);
    auto promise = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) {
        promise->set_value(std::move(result));
    }));

    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_TRUE(result.Error().retryable);
    workflow->Stop();
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, DisconnectAfterReadyClearsStateWithoutSecondCompletion) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) {
        promise->set_value(result.HasValue());
    }));
    ASSERT_TRUE(workflow->MarkReady());
    EXPECT_TRUE(Wait(future));
    EXPECT_TRUE(workflow->IsReady());
    EXPECT_FALSE(workflow->FailActive(MakePxAsyncError(
        PxAsyncErrorCode::kServiceNotConnected, "disconnect", "socket closed", true)));
    EXPECT_FALSE(workflow->IsReady());
    workflow->Stop();
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, StopBeforeReadyCancelsPendingAttempt) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    auto promise = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) {
        promise->set_value(std::move(result));
    }));
    workflow->Stop();
    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kServiceStopped);
    EXPECT_FALSE(workflow->IsReady());
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, DestructionWithQueuedCompletionIsSafe) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) {
        promise->set_value(result.HasValue());
    }));
    ASSERT_TRUE(workflow->MarkReady());
    workflow.reset();
    EXPECT_TRUE(Wait(future));
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, StopFromCompletionCallbackDoesNotSelfJoin) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    std::weak_ptr<PxConnectionAttemptWorkflow> weakWorkflow = workflow;
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt(
        [weakWorkflow, promise](PxConnectionAttemptResult result) {
            if (const auto locked = weakWorkflow.lock()) {
                locked->Stop();
            }
            promise->set_value(result.HasValue());
        }));
    ASSERT_TRUE(workflow->MarkReady());
    EXPECT_TRUE(Wait(future));
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, RepeatedStartStopTenRounds) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = PxAsyncRuntime::Create();
        ASSERT_TRUE(runtime->Start());
        const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();
        ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) {
            promise->set_value(result.HasValue());
        }));
        ASSERT_TRUE(workflow->MarkReady());
        EXPECT_TRUE(Wait(future));
        workflow->Stop();
        EXPECT_FALSE(workflow->BeginAttempt([](PxConnectionAttemptResult) {}));
        EXPECT_FALSE(workflow->MarkReady());
        runtime->RequestStop();
        runtime->Join();
    }
}

} // namespace
} // namespace px
