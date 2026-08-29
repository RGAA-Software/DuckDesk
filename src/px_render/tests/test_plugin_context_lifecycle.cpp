#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px {
namespace {

using namespace std::chrono_literals;

class TestPlugin final : public PxPluginInterface {
public:
    std::string GetPluginId() override { return "test_plugin"; }
    std::string GetPluginName() override { return "test_plugin"; }
};

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

TEST(PluginEventLifecycle, StopInvalidatesAnEventAlreadyQueuedForDelivery) {
    const auto plugin = std::make_shared<TestPlugin>();
    ASSERT_TRUE(plugin->OnCreate(PxPluginParam{}));

    const auto blocker_started = std::make_shared<std::promise<void>>();
    auto blocker_started_future = blocker_started->get_future();
    const auto release_blocker = std::make_shared<std::promise<void>>();
    const auto release_future = release_blocker->get_future().share();
    plugin->GetPluginContext()->PostWorkTask(
        [blocker_started, release_future]() {
            blocker_started->set_value();
            release_future.wait();
        });
    ASSERT_EQ(blocker_started_future.wait_for(5s), std::future_status::ready);

    const auto callback_count = std::make_shared<std::atomic_int>(0);
    plugin->RegisterEventCallback(
        [callback_count](const std::shared_ptr<PxPluginBaseEvent>&) {
            callback_count->fetch_add(1);
        });
    plugin->CallbackEvent(std::make_shared<PxPluginBaseEvent>());
    ASSERT_TRUE(plugin->OnStop());
    release_blocker->set_value();
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(callback_count->load(), 0);
    EXPECT_TRUE(plugin->OnDestroy());
}

TEST(PluginEventLifecycle, CallbackCanUnregisterItselfDuringDispatch) {
    const auto plugin = std::make_shared<TestPlugin>();
    ASSERT_TRUE(plugin->OnCreate(PxPluginParam{}));
    const auto weak_plugin = std::weak_ptr<TestPlugin>(plugin);
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    plugin->RegisterEventCallback(
        [weak_plugin, callback_count, completion](
            const std::shared_ptr<PxPluginBaseEvent>&) {
            callback_count->fetch_add(1);
            if (const auto locked = weak_plugin.lock()) {
                locked->RegisterEventCallback(nullptr);
            }
            completion->set_value();
        });

    plugin->CallbackEvent(std::make_shared<PxPluginBaseEvent>());
    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    plugin->CallbackEvent(std::make_shared<PxPluginBaseEvent>());
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(callback_count->load(), 1);
    EXPECT_TRUE(plugin->OnDestroy());
}

TEST(PluginEventLifecycle, CallbackCanStopPluginDuringDispatch) {
    const auto plugin = std::make_shared<TestPlugin>();
    ASSERT_TRUE(plugin->OnCreate(PxPluginParam{}));
    const auto weak_plugin = std::weak_ptr<TestPlugin>(plugin);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    plugin->RegisterEventCallback(
        [weak_plugin, completion](const std::shared_ptr<PxPluginBaseEvent>&) {
            if (const auto locked = weak_plugin.lock()) {
                locked->OnStop();
            }
            completion->set_value();
        });

    plugin->CallbackEvent(std::make_shared<PxPluginBaseEvent>());
    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(plugin->IsStoppingOrDestroyed());
    EXPECT_TRUE(plugin->OnDestroy());
}

TEST(PluginEventLifecycle, RepeatedCreateDeliverStopDestroyTenRounds) {
    for (int round = 0; round < 10; ++round) {
        const auto plugin = std::make_shared<TestPlugin>();
        ASSERT_TRUE(plugin->OnCreate(PxPluginParam{}));
        const auto callback_count = std::make_shared<std::atomic_int>(0);
        const auto completion = std::make_shared<std::promise<void>>();
        auto completed = completion->get_future();
        plugin->RegisterEventCallback(
            [callback_count, completion](
                const std::shared_ptr<PxPluginBaseEvent>&) {
                callback_count->fetch_add(1);
                completion->set_value();
            });
        plugin->CallbackEvent(std::make_shared<PxPluginBaseEvent>());
        ASSERT_EQ(completed.wait_for(5s), std::future_status::ready)
            << "round " << round;
        EXPECT_EQ(callback_count->load(), 1) << "round " << round;
        EXPECT_TRUE(plugin->OnStop()) << "round " << round;
        EXPECT_TRUE(plugin->OnDestroy()) << "round " << round;
    }
}

} // namespace
} // namespace px
