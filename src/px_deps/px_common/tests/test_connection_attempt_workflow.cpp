#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "px_common/connection_attempt_workflow.h"

namespace px {
namespace {

using namespace std::chrono_literals;

template <typename T> T Wait(std::future<T>& future) {
    EXPECT_EQ(future.wait_for(3s), std::future_status::ready);
    return future.get();
}

PxAwaitable<void> WaitForReady(std::shared_ptr<PxConnectionAttemptWorkflow> workflow, PxConnectionAttemptTicket ticket,
                               std::chrono::steady_clock::time_point deadline, std::shared_ptr<std::promise<PxConnectionAttemptResult>> completion) {
    completion->set_value(co_await PxConnectionAttemptWorkflow::WaitUntilReady(std::move(workflow), ticket, deadline));
}

PxAwaitable<void> WaitForDisconnected(std::shared_ptr<PxConnectionAttemptWorkflow> workflow, PxConnectionAttemptTicket ticket,
                                      std::shared_ptr<std::promise<PxConnectionDisconnectedResult>> completion) {
    completion->set_value(co_await PxConnectionAttemptWorkflow::WaitUntilDisconnected(std::move(workflow), ticket));
}

TEST(ConnectionAttemptWorkflow, AwaitableFirstAttemptCompletesWithGeneration) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    const auto start = workflow->StartAttempt();
    ASSERT_TRUE(start);

    const auto completion = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("await-connection-ready", [workflow, ticket = start.Value(), completion]() {
        return WaitForReady(workflow, ticket, std::chrono::steady_clock::now() + 1s, completion);
    }));
    EXPECT_TRUE(workflow->MarkReady(start.Value().generation));

    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value().generation, start.Value().generation);
    EXPECT_TRUE(workflow->IsReady());

    workflow->Stop();
    ASSERT_TRUE(scope->StopAndWait(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, AwaitableFirstRejectsStaleGeneration) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    const auto first = workflow->StartAttempt();
    const auto second = workflow->StartAttempt();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_FALSE(workflow->MarkReady(first.Value().generation));

    const auto first_completion = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    const auto second_completion = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto first_future = first_completion->get_future();
    auto second_future = second_completion->get_future();
    ASSERT_TRUE(scope->Spawn("await-stale-connection", [workflow, ticket = first.Value(), first_completion]() {
        return WaitForReady(workflow, ticket, std::chrono::steady_clock::now() + 1s, first_completion);
    }));
    ASSERT_TRUE(scope->Spawn("await-active-connection", [workflow, ticket = second.Value(), second_completion]() {
        return WaitForReady(workflow, ticket, std::chrono::steady_clock::now() + 1s, second_completion);
    }));
    EXPECT_TRUE(workflow->MarkReady(second.Value().generation));

    auto first_result = Wait(first_future);
    auto second_result = Wait(second_future);
    ASSERT_FALSE(first_result);
    EXPECT_EQ(first_result.Error().StableCode(), "CONNECTION_ATTEMPT_STALE_GENERATION");
    ASSERT_TRUE(second_result);
    EXPECT_EQ(second_result.Value().generation, second.Value().generation);

    workflow->Stop();
    ASSERT_TRUE(scope->StopAndWait(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, AwaitableFirstDeadlineClearsActiveAttempt) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    const auto start = workflow->StartAttempt();
    ASSERT_TRUE(start);

    const auto completion = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("await-connection-timeout", [workflow, ticket = start.Value(), completion]() {
        return WaitForReady(workflow, ticket, std::chrono::steady_clock::now() + 20ms, completion);
    }));
    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_FALSE(workflow->MarkReady(start.Value().generation));
    EXPECT_FALSE(workflow->IsReady());

    workflow->Stop();
    ASSERT_TRUE(scope->StopAndWait(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, ReadyCompletesExactlyOnce) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    ASSERT_TRUE(workflow);

    auto promise = std::make_shared<std::promise<PxConnectionAttemptResult>>();
    auto future = promise->get_future();
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) { promise->set_value(std::move(result)); }));
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
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) { promise->set_value(std::move(result)); }));
    EXPECT_TRUE(workflow->FailActive(MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "upgrade", "upgrade rejected", true)));
    EXPECT_FALSE(workflow->FailActive(MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "disconnect", "late disconnect")));

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
    ASSERT_TRUE(workflow->BeginAttempt([firstPromise](PxConnectionAttemptResult result) { firstPromise->set_value(std::move(result)); }));
    ASSERT_TRUE(workflow->BeginAttempt([secondPromise](PxConnectionAttemptResult result) { secondPromise->set_value(std::move(result)); }));
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
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) { promise->set_value(std::move(result)); }));

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
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) { promise->set_value(result.HasValue()); }));
    ASSERT_TRUE(workflow->MarkReady());
    EXPECT_TRUE(Wait(future));
    EXPECT_TRUE(workflow->IsReady());
    EXPECT_FALSE(workflow->FailActive(MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "disconnect", "socket closed", true)));
    EXPECT_FALSE(workflow->IsReady());
    workflow->Stop();
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, AwaitableDisconnectCompletesForReadyGeneration) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    const auto start = workflow->StartAttempt();
    ASSERT_TRUE(start);
    ASSERT_TRUE(workflow->MarkReady(start.Value().generation));

    const auto completion = std::make_shared<std::promise<PxConnectionDisconnectedResult>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("await-disconnect", [workflow, ticket = start.Value(), completion]() {
        return WaitForDisconnected(workflow, ticket, completion);
    }));
    const auto reason = MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "disconnect", "socket closed", true);
    EXPECT_TRUE(workflow->MarkDisconnected(start.Value().generation, reason));

    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value().generation, start.Value().generation);
    EXPECT_EQ(result.Value().reason.stage, "disconnect");
    EXPECT_FALSE(workflow->IsReady());

    workflow->Stop();
    ASSERT_TRUE(scope->StopAndWait(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(ConnectionAttemptWorkflow, DisconnectRejectsStaleGeneration) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto workflow = PxConnectionAttemptWorkflow::Create(runtime, 1s);
    const auto first = workflow->StartAttempt();
    const auto second = workflow->StartAttempt();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_FALSE(workflow->MarkDisconnected(
        first.Value().generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "disconnect", "stale")));

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
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) { promise->set_value(std::move(result)); }));
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
    ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) { promise->set_value(result.HasValue()); }));
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
    ASSERT_TRUE(workflow->BeginAttempt([weakWorkflow, promise](PxConnectionAttemptResult result) {
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
        ASSERT_TRUE(workflow->BeginAttempt([promise](PxConnectionAttemptResult result) { promise->set_value(result.HasValue()); }));
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
