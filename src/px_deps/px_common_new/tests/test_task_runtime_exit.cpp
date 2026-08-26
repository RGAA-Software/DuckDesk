#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "../task_runtime.h"

using namespace std::chrono_literals;

namespace px {

TEST(TaskRuntimeExit, RejectsTasksAfterExit) {
    TaskRuntime runtime(2);
    runtime.Exit();

    auto ran = std::make_shared<std::atomic_bool>(false);
    const auto task_id = runtime.Post(SimpleThreadTask::Make([ran]() { *ran = true; }));

    EXPECT_EQ(task_id, 0);
    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(*ran);
}

TEST(TaskRuntimeExit, ExitWaitsForRunningTaskAndCancelsQueuedTask) {
    auto runtime = std::make_shared<TaskRuntime>(1);
    auto started = std::make_shared<std::promise<void>>();
    auto release = std::make_shared<std::promise<void>>();
    auto release_signal = release->get_future().share();
    auto queued_ran = std::make_shared<std::atomic_bool>(false);

    ASSERT_NE(runtime->Post(SimpleThreadTask::Make([started, release_signal]() {
        started->set_value();
        release_signal.wait();
    })), 0);
    ASSERT_NE(runtime->Post(SimpleThreadTask::Make([queued_ran]() { *queued_ran = true; })), 0);
    ASSERT_EQ(started->get_future().wait_for(2s), std::future_status::ready);

    auto exit_result = std::async(std::launch::async, [runtime]() { runtime->Exit(); });
    EXPECT_EQ(exit_result.wait_for(30ms), std::future_status::timeout);
    release->set_value();
    EXPECT_EQ(exit_result.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(*queued_ran);
}

TEST(TaskRuntimeExit, ShutdownFromOwnCallbackIsLifetimeSafe) {
    auto stopped = std::make_shared<std::promise<void>>();
    auto runtime = std::make_shared<TaskRuntime>(1);
    std::weak_ptr<TaskRuntime> weak_runtime = runtime;

    ASSERT_NE(runtime->Post(SimpleThreadTask::Make([runtime, stopped]() {
        runtime->Exit();
        stopped->set_value();
    })), 0);
    runtime.reset();

    ASSERT_EQ(stopped->get_future().wait_for(2s), std::future_status::ready);
    for (int attempt = 0; attempt < 100 && !weak_runtime.expired(); ++attempt) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(weak_runtime.expired());
}

} // namespace px
