#include <gtest/gtest.h>
#include "rd_context.h"
#include <chrono>
#include <memory>
#include <thread>

namespace {

TEST(RenderContextShutdown, PendingDelayIsCancelledAndCapturedOwnerReleased) {
    for (int iteration{0}; iteration < 20; ++iteration) {
        auto context = px::RdContext::Make();
        auto token = std::make_shared<int>(0);
        const std::weak_ptr<int> observer{token};
        context->PostDelayTask([token] { ADD_FAILURE() << "Cancelled task executed"; }, 60000);
        token.reset();
        context.reset();
        EXPECT_TRUE(observer.expired());
    }
}

TEST(RenderContextShutdown, UiCallbackCanReleaseLastContextOwner) {
    for (int iteration{0}; iteration < 20; ++iteration) {
        auto context = px::RdContext::Make();
        const std::weak_ptr<px::RdContext> observer{context};
        const auto slot = std::make_shared<std::shared_ptr<px::RdContext>>(context);
        context->PostDelayTask(
            [weak_slot = std::weak_ptr{slot}] {
                if (const auto owner = weak_slot.lock()) {
                    owner->reset();
                }
            },
            1);
        context.reset();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (*slot && std::chrono::steady_clock::now() < deadline) {
            // The dispatch call retains ownership until its current callback returns.
            const auto dispatch_owner = *slot;
            dispatch_owner->ExecutePendingUITasks();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EXPECT_FALSE(*slot);
        EXPECT_TRUE(observer.expired());
    }
}

} // namespace
