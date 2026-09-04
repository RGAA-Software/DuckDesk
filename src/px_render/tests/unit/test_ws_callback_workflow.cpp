#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "px_common_new/async_runtime.h"
#include "plugins/net_ws/ws_callback_workflow.h"

namespace px {
namespace {

using namespace std::chrono_literals;

struct CallbackSlot final {
    std::mutex mutex;
    std::function<void(int)> callback;
};

std::function<void(int)> WaitForCallback(
    const std::shared_ptr<CallbackSlot>& slot) {
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard lock(slot->mutex);
            if (slot->callback) {
                return slot->callback;
            }
        }
        std::this_thread::sleep_for(1ms);
    }
    return {};
}

PxAwaitable<void> RunValueWorkflow(
    std::shared_ptr<std::promise<PxResult<int>>> completion,
    std::shared_ptr<CallbackSlot> slot,
    std::shared_ptr<int> late_value,
    const std::chrono::steady_clock::time_point deadline) {
    auto result = co_await AwaitWsValueCallback<int>(
        [slot](std::function<void(int)> callback) {
            std::lock_guard lock(slot->mutex);
            slot->callback = std::move(callback);
            return true;
        },
        deadline,
        "test_ws_callback",
        [late_value](const int value) {
            *late_value = value;
        });
    completion->set_value(std::move(result));
}

TEST(WsCallbackWorkflowTest, TypedCompletionResumesAwaiter) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto slot = std::make_shared<CallbackSlot>();
    const auto late_value = std::make_shared<int>(0);
    const auto completion =
        std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("typed-completion", [=] {
        return RunValueWorkflow(
            completion, slot, late_value,
            std::chrono::steady_clock::now() + 1s);
    }));
    const auto callback = WaitForCallback(slot);
    ASSERT_TRUE(callback);
    callback(42);
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 42);
    EXPECT_EQ(*late_value, 0);
    EXPECT_TRUE(scope->StopAndWait(1s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(WsCallbackWorkflowTest, TimeoutRoutesLateCompletionToCompensation) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto slot = std::make_shared<CallbackSlot>();
    const auto late_value = std::make_shared<int>(0);
    const auto completion =
        std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("late-completion", [=] {
        return RunValueWorkflow(
            completion, slot, late_value,
            std::chrono::steady_clock::now() + 20ms);
    }));
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    auto result = future.get();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    const auto callback = WaitForCallback(slot);
    ASSERT_TRUE(callback);
    callback(77);
    EXPECT_EQ(*late_value, 77);
    EXPECT_TRUE(scope->StopAndWait(1s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(WsCallbackWorkflowTest, ScopeCancellationIsSafeBeforeLateCompletion) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto slot = std::make_shared<CallbackSlot>();
    const auto late_value = std::make_shared<int>(0);
    const auto completion =
        std::make_shared<std::promise<PxResult<int>>>();
    auto future = completion->get_future();
    ASSERT_TRUE(scope->Spawn("cancelled-completion", [=] {
        return RunValueWorkflow(
            completion, slot, late_value,
            std::chrono::steady_clock::now() + 5s);
    }));
    const auto callback = WaitForCallback(slot);
    ASSERT_TRUE(callback);
    scope->BeginStop();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    auto result = future.get();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kCancelled);
    callback(99);
    EXPECT_EQ(*late_value, 99);
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestStop();
    runtime->Join();
}

}  // namespace
}  // namespace px
