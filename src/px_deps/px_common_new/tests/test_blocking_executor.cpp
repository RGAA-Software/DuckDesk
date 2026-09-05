#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

#include "../blocking_executor.h"

namespace px {
namespace {

using namespace std::chrono_literals;

TEST(BlockingExecutorTest, ExecutesTasksAndCollectsStatistics) {
    const auto executor = PxBlockingExecutor::Create({.thread_count = 2, .max_pending_tasks = 8});
    const auto completed = std::make_shared<std::promise<void>>();

    ASSERT_EQ(executor->TryPost([completed]() { completed->set_value(); }), PxBlockingSubmitResult::kAccepted);
    ASSERT_EQ(completed->get_future().wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(executor->WaitForIdle(2s));

    const auto statistics = executor->GetStatistics();
    EXPECT_EQ(statistics.submitted, 1);
    EXPECT_EQ(statistics.completed, 1);
    EXPECT_EQ(statistics.failed, 0);
    executor->RequestStop();
    executor->Join();
}

TEST(BlockingExecutorTest, RejectsWhenBoundedQueueIsFull) {
    const auto executor = PxBlockingExecutor::Create({.thread_count = 1, .max_pending_tasks = 1});
    const auto entered = std::make_shared<std::promise<void>>();
    const auto release = std::make_shared<std::promise<void>>();
    const auto release_future = release->get_future().share();

    ASSERT_EQ(executor->TryPost([entered, release_future]() {
        entered->set_value();
        release_future.wait();
    }), PxBlockingSubmitResult::kAccepted);
    ASSERT_EQ(entered->get_future().wait_for(2s), std::future_status::ready);
    ASSERT_EQ(executor->TryPost([]() {}), PxBlockingSubmitResult::kAccepted);
    EXPECT_EQ(executor->TryPost([]() {}), PxBlockingSubmitResult::kQueueFull);

    release->set_value();
    ASSERT_TRUE(executor->WaitForIdle(2s));
    executor->RequestStop();
    executor->Join();
    EXPECT_EQ(executor->GetStatistics().rejected, 1);
}

TEST(BlockingExecutorTest, CatchesTaskFailureAndCancelsPendingWork) {
    const auto executor = PxBlockingExecutor::Create({.thread_count = 1, .max_pending_tasks = 4});
    const auto entered = std::make_shared<std::promise<void>>();
    const auto release = std::make_shared<std::promise<void>>();
    const auto release_future = release->get_future().share();

    ASSERT_EQ(executor->TryPost([entered, release_future]() {
        entered->set_value();
        release_future.wait();
        throw std::runtime_error("expected test failure");
    }), PxBlockingSubmitResult::kAccepted);
    ASSERT_EQ(entered->get_future().wait_for(2s), std::future_status::ready);
    ASSERT_EQ(executor->TryPost([]() {}), PxBlockingSubmitResult::kAccepted);

    executor->RequestStop(PxBlockingShutdownMode::kCancelPending);
    release->set_value();
    executor->Join();
    const auto statistics = executor->GetStatistics();
    EXPECT_EQ(statistics.failed, 1);
    EXPECT_EQ(statistics.cancelled, 1);
    EXPECT_EQ(executor->TryPost([]() {}), PxBlockingSubmitResult::kStopped);
}

TEST(BlockingExecutorTest, CanBeDestroyedFromWorkerTask) {
    auto executor = PxBlockingExecutor::Create({.thread_count = 1, .max_pending_tasks = 2});
    const auto destroyed = std::make_shared<std::promise<void>>();
    const auto holder = std::make_shared<std::shared_ptr<PxBlockingExecutor>>(executor);
    const std::weak_ptr<PxBlockingExecutor> weak_executor = executor;

    ASSERT_EQ(executor->TryPost([holder, destroyed]() {
        holder->reset();
        destroyed->set_value();
    }), PxBlockingSubmitResult::kAccepted);
    executor.reset();
    ASSERT_EQ(destroyed->get_future().wait_for(2s), std::future_status::ready);
    for (int attempt = 0; attempt < 100 && !weak_executor.expired(); ++attempt) {
        std::this_thread::yield();
    }
    EXPECT_TRUE(weak_executor.expired());
}

} // namespace
} // namespace px
