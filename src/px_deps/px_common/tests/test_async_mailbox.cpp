#include <chrono>
#include <future>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "px_common/async_mailbox.h"

namespace px {
namespace {

using namespace std::chrono_literals;
using StringMailbox = PxAsyncMailbox<std::string>;

PxAwaitable<void> Receive(std::shared_ptr<StringMailbox> mailbox, std::chrono::steady_clock::time_point deadline,
                          std::shared_ptr<std::promise<PxResult<std::string>>> completion) {
    completion->set_value(co_await StringMailbox::ReceiveUntil(std::move(mailbox), deadline));
}

PxResult<std::string> Wait(std::future<PxResult<std::string>>& future) {
    EXPECT_EQ(future.wait_for(3s), std::future_status::ready);
    return future.get();
}

TEST(PxAsyncMailbox, RejectsZeroCapacity) {
    const auto runtime = PxAsyncRuntime::Create();
    EXPECT_FALSE(StringMailbox::Create(runtime->Executor(PxAsyncLane::kState), 0));
}

TEST(PxAsyncMailbox, QueuedValueIsReceivedAndStatisticsAreBounded) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto mailbox = StringMailbox::Create(scope->Executor(), 2);
    ASSERT_TRUE(mailbox);

    EXPECT_TRUE(mailbox->TryPush("first"));
    EXPECT_TRUE(mailbox->TryPush("second"));
    const auto full = mailbox->TryPush("third");
    ASSERT_FALSE(full);
    EXPECT_EQ(full.Error().code, PxAsyncErrorCode::kQueueFull);

    const auto completion = std::make_shared<std::promise<PxResult<std::string>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(
        scope->Spawn("mailbox-receive", [mailbox, completion]() { return Receive(mailbox, std::chrono::steady_clock::now() + 1s, completion); }));

    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value(), "first");
    const auto statistics = mailbox->Statistics();
    EXPECT_EQ(statistics.accepted, 2U);
    EXPECT_EQ(statistics.received, 1U);
    EXPECT_EQ(statistics.rejected_full, 1U);
    EXPECT_EQ(statistics.depth, 1U);
    EXPECT_EQ(statistics.high_watermark, 2U);

    EXPECT_TRUE(mailbox->Close(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "test", "test stopped")));
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncMailbox, WaitingReceiverGetsPublishedValueExactlyOnce) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto mailbox = StringMailbox::Create(scope->Executor(), 1);
    const auto completion = std::make_shared<std::promise<PxResult<std::string>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(
        scope->Spawn("mailbox-wait", [mailbox, completion]() { return Receive(mailbox, std::chrono::steady_clock::now() + 1s, completion); }));
    EXPECT_TRUE(mailbox->TryPush("ready"));
    auto result = Wait(future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value(), "ready");
    EXPECT_EQ(mailbox->Statistics().received, 1U);

    EXPECT_TRUE(mailbox->Close(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "test", "test stopped")));
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncMailbox, TimeoutDoesNotConsumeLateValue) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto mailbox = StringMailbox::Create(scope->Executor(), 1);
    const auto first_completion = std::make_shared<std::promise<PxResult<std::string>>>();
    auto first_future = first_completion->get_future();

    ASSERT_TRUE(scope->Spawn("mailbox-timeout",
                             [mailbox, first_completion]() { return Receive(mailbox, std::chrono::steady_clock::now() + 20ms, first_completion); }));
    auto timeout = Wait(first_future);
    ASSERT_FALSE(timeout);
    EXPECT_EQ(timeout.Error().code, PxAsyncErrorCode::kTimeout);
    EXPECT_TRUE(mailbox->TryPush("late"));

    const auto second_completion = std::make_shared<std::promise<PxResult<std::string>>>();
    auto second_future = second_completion->get_future();
    ASSERT_TRUE(scope->Spawn("mailbox-after-timeout",
                             [mailbox, second_completion]() { return Receive(mailbox, std::chrono::steady_clock::now() + 1s, second_completion); }));
    auto result = Wait(second_future);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Value(), "late");

    EXPECT_TRUE(mailbox->Close(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "test", "test stopped")));
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncMailbox, CloseFailsWaiterRejectsPushAndIsIdempotent) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto mailbox = StringMailbox::Create(scope->Executor(), 1);
    const auto completion = std::make_shared<std::promise<PxResult<std::string>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(
        scope->Spawn("mailbox-close", [mailbox, completion]() { return Receive(mailbox, std::chrono::steady_clock::now() + 1h, completion); }));
    const auto close_error = MakePxAsyncError(PxAsyncErrorCode::kCancelled, "test.close", "test cancelled");
    EXPECT_TRUE(mailbox->Close(close_error));
    EXPECT_FALSE(mailbox->Close(close_error));

    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    const auto rejected = mailbox->TryPush("rejected");
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.Error().code, PxAsyncErrorCode::kCancelled);
    EXPECT_TRUE(mailbox->IsClosed());
    EXPECT_EQ(mailbox->Statistics().rejected_closed, 1U);

    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(PxAsyncMailbox, ScopeCancellationReleasesWaitingReceiver) {
    const auto runtime = PxAsyncRuntime::Create();
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto mailbox = StringMailbox::Create(scope->Executor(), 1);
    const auto completion = std::make_shared<std::promise<PxResult<std::string>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(
        scope->Spawn("mailbox-cancel", [mailbox, completion]() { return Receive(mailbox, std::chrono::steady_clock::now() + 1h, completion); }));
    scope->BeginStop();
    auto result = Wait(future);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    ASSERT_TRUE(scope->WaitFor(2s));

    EXPECT_TRUE(mailbox->Close(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "test", "test stopped")));
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px
