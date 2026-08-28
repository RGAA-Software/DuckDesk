#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_render/plugin_interface/px_plugin_context.h"

namespace px {
namespace {

using namespace std::chrono_literals;

TEST(PluginContextLifecycle, DestroyFromTimerCallbackDoesNotSelfJoin) {
    auto context = std::make_shared<PxPluginContext>("timer-self-stop-test");
    const auto weak_context = std::weak_ptr<PxPluginContext>(context);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();

    context->StartTimer(1, [weak_context, completion]() {
        if (const auto locked = weak_context.lock()) {
            locked->OnDestroy();
        }
        completion->set_value();
    });

    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    context.reset();
    std::this_thread::sleep_for(20ms);
}

TEST(PluginContextLifecycle, DestroyFromWorkCallbackDoesNotSelfJoin) {
    auto context = std::make_shared<PxPluginContext>("worker-self-stop-test");
    const auto weak_context = std::weak_ptr<PxPluginContext>(context);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();

    context->PostWorkTask([weak_context, completion]() {
        if (const auto locked = weak_context.lock()) {
            locked->OnDestroy();
        }
        completion->set_value();
    });

    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    context.reset();
    std::this_thread::sleep_for(20ms);
}

TEST(PluginContextLifecycle, DestroyCancelsQueuedDelayCallbacks) {
    auto context = std::make_shared<PxPluginContext>("queued-delay-stop-test");
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    for (int index = 0; index < 64; ++index) {
        context->PostDelayTask(
            [callback_count]() { callback_count->fetch_add(1); }, 200);
    }

    context->OnDestroy();
    context.reset();
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(callback_count->load(), 0);
}

TEST(PluginContextLifecycle, RepeatedConstructStartAndDestroy) {
    for (int round = 0; round < 10; ++round) {
        auto context = std::make_shared<PxPluginContext>(
            "repeated-context-" + std::to_string(round));
        const auto callback_count = std::make_shared<std::atomic_int>(0);
        context->PostWorkTask(
            [callback_count]() { callback_count->fetch_add(1); });
        context->PostDelayTask(
            [callback_count]() { callback_count->fetch_add(1); }, 100);
        context->OnDestroy();
        context.reset();
    }
}

} // namespace
} // namespace px
