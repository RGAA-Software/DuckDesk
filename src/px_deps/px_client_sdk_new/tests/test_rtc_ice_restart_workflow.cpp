#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>

#include "connection/rtc_ice_restart_workflow.h"
#include "px_common_new/async_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> AwaitRestart(
    std::shared_ptr<RtcIceRestartWorkflow::Operation> operation,
    std::chrono::steady_clock::time_point deadline,
    std::shared_ptr<std::promise<PxResult<RtcIceRestartCompletion>>> completion) {
    completion->set_value(
        co_await RtcIceRestartWorkflow::Operation::WaitUntil(operation, deadline));
    co_return;
}

class RtcIceRestartWorkflowTest : public testing::Test {
protected:
    void SetUp() override {
        runtime_ = PxAsyncRuntime::Create({.worker_threads = 1});
        ASSERT_TRUE(runtime_->Start());
        scope_ = PxAsyncScope::Create(runtime_, PxAsyncLane::kState);
        workflow_ = RtcIceRestartWorkflow::Create(scope_->Executor());
    }

    void TearDown() override {
        if (workflow_) {
            static_cast<void>(workflow_->Cancel(MakePxAsyncError(
                PxAsyncErrorCode::kServiceStopped, "test_stop", "test is stopping")));
        }
        if (scope_) {
            ASSERT_TRUE(scope_->StopAndWait(2s));
        }
        if (runtime_) {
            runtime_->RequestStop();
            runtime_->Join();
        }
    }

    std::future<PxResult<RtcIceRestartCompletion>> WaitFor(
        const RtcIceRestartBegin& begin,
        std::chrono::milliseconds timeout = 2s) {
        auto completion =
            std::make_shared<std::promise<PxResult<RtcIceRestartCompletion>>>();
        auto future = completion->get_future();
        const auto operation = begin.operation;
        EXPECT_TRUE(scope_->Spawn("rtc-ice-restart-test",
            [operation, completion, deadline = std::chrono::steady_clock::now() + timeout]() {
                return AwaitRestart(operation, deadline, completion);
            }));
        return future;
    }

    std::shared_ptr<PxAsyncRuntime> runtime_;
    std::shared_ptr<PxAsyncScope> scope_;
    std::shared_ptr<RtcIceRestartWorkflow> workflow_;
};

TEST_F(RtcIceRestartWorkflowTest, RequestApplyAndConnectedHaveOneSuccessfulTerminal) {
    const auto request = workflow_->BeginConfigurationRequest();
    ASSERT_TRUE(request.StartedWorkflow());
    auto future = WaitFor(request);

    const auto apply = workflow_->ApplyConfiguration(7);
    EXPECT_EQ(apply.disposition, RtcIceRestartDisposition::kUpdated);
    ASSERT_TRUE(workflow_->MarkApplyAccepted(apply.generation, apply.apply_sequence));
    ASSERT_TRUE(workflow_->CompleteConnected());
    EXPECT_FALSE(workflow_->CompleteConnected());

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().generation, request.generation);
    EXPECT_EQ(result.Value().revision, 7U);
    EXPECT_EQ(workflow_->Snapshot().stage, RtcIceRestartStage::kIdle);
    EXPECT_EQ(workflow_->Snapshot().last_completed_revision, 7U);
}

TEST_F(RtcIceRestartWorkflowTest, DuplicateAndCompletedRevisionsDoNotApplyAgain) {
    const auto begin = workflow_->ApplyConfiguration(11);
    ASSERT_TRUE(begin.StartedWorkflow());
    auto future = WaitFor(begin);
    const auto duplicate = workflow_->ApplyConfiguration(11);
    EXPECT_EQ(duplicate.disposition, RtcIceRestartDisposition::kDuplicate);
    EXPECT_FALSE(duplicate.ShouldApplyConfiguration());
    ASSERT_TRUE(workflow_->MarkApplyAccepted(begin.generation, begin.apply_sequence));
    ASSERT_TRUE(workflow_->CompleteConnected());
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(future.get().HasValue());

    const auto stale = workflow_->ApplyConfiguration(11);
    EXPECT_EQ(stale.disposition, RtcIceRestartDisposition::kStale);
    EXPECT_FALSE(stale.ShouldApplyConfiguration());
    EXPECT_EQ(workflow_->Snapshot().stage, RtcIceRestartStage::kIdle);
}

TEST_F(RtcIceRestartWorkflowTest, NewerRevisionMakesLateApplyFailureHarmless) {
    const auto first = workflow_->ApplyConfiguration(20);
    ASSERT_TRUE(first.StartedWorkflow());
    auto future = WaitFor(first);
    const auto second = workflow_->ApplyConfiguration(21);
    ASSERT_EQ(second.disposition, RtcIceRestartDisposition::kUpdated);
    EXPECT_GT(second.apply_sequence, first.apply_sequence);

    EXPECT_FALSE(workflow_->MarkApplyFailed(
        first.generation, first.apply_sequence,
        MakePxAsyncError(PxAsyncErrorCode::kServiceRejected,
                         "set_configuration", "old apply failed")));
    ASSERT_TRUE(workflow_->MarkApplyAccepted(second.generation, second.apply_sequence));
    ASSERT_TRUE(workflow_->CompleteConnected());
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().revision, 21U);
}

TEST_F(RtcIceRestartWorkflowTest, ApplyFailureIsTerminalAndLateIceIsIgnored) {
    const auto begin = workflow_->ApplyConfiguration(30);
    ASSERT_TRUE(begin.StartedWorkflow());
    auto future = WaitFor(begin);
    ASSERT_TRUE(workflow_->MarkApplyFailed(
        begin.generation, begin.apply_sequence,
        MakePxAsyncError(PxAsyncErrorCode::kServiceRejected,
                         "set_configuration", "libwebrtc rejected configuration")));
    EXPECT_FALSE(workflow_->CompleteConnected());
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kServiceRejected);
}

TEST_F(RtcIceRestartWorkflowTest, TimeoutDetachesGenerationAndLateCallbacksAreIgnored) {
    const auto begin = workflow_->BeginConfigurationRequest();
    ASSERT_TRUE(begin.StartedWorkflow());
    auto future = WaitFor(begin, 30ms);
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_TRUE(workflow_->DetachTimedOut(begin.generation, begin.operation));
    EXPECT_FALSE(workflow_->MarkApplyAccepted(begin.generation, 1));
    EXPECT_FALSE(workflow_->CompleteConnected());
}

TEST_F(RtcIceRestartWorkflowTest, CancelAndRepeatedLifecycleRemainSafe) {
    for (int round = 0; round < 10; ++round) {
        const auto begin = workflow_->BeginConfigurationRequest();
        ASSERT_TRUE(begin.StartedWorkflow()) << "round=" << round;
        auto future = WaitFor(begin);
        ASSERT_TRUE(workflow_->Cancel(MakePxAsyncError(
            PxAsyncErrorCode::kCancelled, "shutdown", "connection stopped")));
        EXPECT_FALSE(workflow_->Cancel(MakePxAsyncError(
            PxAsyncErrorCode::kCancelled, "shutdown", "duplicate stop")));
        ASSERT_EQ(future.wait_for(2s), std::future_status::ready) << "round=" << round;
        const auto result = future.get();
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    }
}

} // namespace
} // namespace px
