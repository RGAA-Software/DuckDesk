#include <gtest/gtest.h>
#include "app/render_ui_task_queue.h"
#include <memory>
#include <thread>

namespace {

TEST(RenderUiTaskQueue, WorkerPostWakesAnOtherwiseIdleMessageLoop) {
    const auto queue = std::make_shared<px::render::UiTaskQueue>();
    MSG message{};
    while (PeekMessageW(&message, nullptr, WM_NULL, WM_NULL, PM_REMOVE)) {
    }
    const auto calls = std::make_shared<int>(0);
    std::jthread producer{[queue, calls] { EXPECT_TRUE(queue->Post([calls] { ++*calls; })); }};
    const auto result = MsgWaitForMultipleObjectsEx(0, nullptr, 2000, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
    EXPECT_EQ(result, WAIT_OBJECT_0);
    producer.join();
    queue->Drain();
    EXPECT_EQ(*calls, 1);
}

TEST(RenderUiTaskQueue, CloseDuringDispatchCancelsRemainingCallbacks) {
    const auto queue = std::make_shared<px::render::UiTaskQueue>();
    const auto calls = std::make_shared<int>(0);
    ASSERT_TRUE(queue->Post([weak = std::weak_ptr{queue}] {
        if (const auto owner = weak.lock()) {
            owner->Close();
        }
    }));
    ASSERT_TRUE(queue->Post([calls] { ++*calls; }));
    queue->Drain();
    EXPECT_EQ(*calls, 0);
    EXPECT_FALSE(queue->Post([calls] { ++*calls; }));
    queue->Close();
}

TEST(RenderUiTaskQueue, DestructionReleasesQueuedCapturesWithoutRunningThem) {
    for (int iteration{0}; iteration < 16; ++iteration) {
        auto queue = std::make_shared<px::render::UiTaskQueue>();
        auto token = std::make_shared<int>(0);
        const std::weak_ptr<int> observer{token};
        ASSERT_TRUE(queue->Post([token] { ADD_FAILURE() << "Destroyed queue must cancel its callbacks"; }));
        token.reset();
        queue.reset();
        EXPECT_TRUE(observer.expired());
    }
}

} // namespace
