#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <QCoreApplication>

#include "px_message.pb.h"
#include "px_client/plugin_interface/ct_plugin_context.h"
#include "px_client/plugin_interface/ct_plugin_interface.h"
#include "px_client/plugin_interface/ct_plugin_events.h"

namespace px {
namespace {

using namespace std::chrono_literals;

class TestClientPlugin final : public ClientPluginInterface {
public:
    std::string GetPluginId() override { return "test_client_plugin"; }
    std::string GetPluginName() override { return "test_client_plugin"; }

    void StartWithoutUiForTest() {
        plugin_context_ = std::make_shared<ClientPluginContext>(GetPluginName());
        stopped_ = false;
        destroyed_ = false;
        lifecycle_state_ = ClientPluginLifecycleState::Running;
    }
};

TEST(ClientPluginContextLifecycle, DestroyFromTimerCallbackDoesNotSelfJoin) {
    const auto context =
        std::make_shared<ClientPluginContext>("client-timer-self-stop");
    const auto weak_context = std::weak_ptr<ClientPluginContext>(context);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    context->StartTimer(
        1, [weak_context, completion]() {
            if (const auto locked = weak_context.lock()) {
                locked->OnDestroy();
            }
            completion->set_value();
        });

    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    context->OnDestroy();
}

TEST(ClientPluginContextLifecycle, DestroyFromWorkCallbackDoesNotSelfJoin) {
    const auto context =
        std::make_shared<ClientPluginContext>("client-worker-self-stop");
    const auto weak_context = std::weak_ptr<ClientPluginContext>(context);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    context->PostWorkTask(
        [weak_context, completion]() {
            if (const auto locked = weak_context.lock()) {
                locked->OnDestroy();
            }
            completion->set_value();
        });

    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    context->OnDestroy();
}

TEST(ClientPluginContextLifecycle, DestroyCancelsDelayedCallbacks) {
    const auto context =
        std::make_shared<ClientPluginContext>("client-delay-cancel");
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    for (int index = 0; index < 64; ++index) {
        context->PostDelayTask(
            [callback_count]() { callback_count->fetch_add(1); }, 200);
    }

    context->OnDestroy();
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(callback_count->load(), 0);
}

TEST(ClientPluginContextLifecycle, DestroyCancelsQueuedUiCallbacks) {
    auto context =
        std::make_shared<ClientPluginContext>("client-ui-cancel");
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    std::jthread producer([context, callback_count]() {
        for (int index = 0; index < 64; ++index) {
            context->PostUITask(
                [callback_count]() { callback_count->fetch_add(1); });
        }
    });
    producer.join();

    context->OnDestroy();
    context.reset();
    QCoreApplication::processEvents();
    EXPECT_EQ(callback_count->load(), 0);
}

TEST(ClientPluginEventLifecycle, StopInvalidatesQueuedDelivery) {
    const auto plugin = std::make_shared<TestClientPlugin>();
    plugin->StartWithoutUiForTest();
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
        [callback_count](const std::shared_ptr<ClientPluginBaseEvent>&) {
            callback_count->fetch_add(1);
        });
    plugin->CallbackEvent(std::make_shared<ClientPluginBaseEvent>());
    ASSERT_TRUE(plugin->OnStop());
    release_blocker->set_value();
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(callback_count->load(), 0);
    EXPECT_TRUE(plugin->OnDestroy());
}

TEST(ClientPluginEventLifecycle, CallbackCanUnregisterItself) {
    const auto plugin = std::make_shared<TestClientPlugin>();
    plugin->StartWithoutUiForTest();
    const auto weak_plugin = std::weak_ptr<TestClientPlugin>(plugin);
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    plugin->RegisterEventCallback(
        [weak_plugin, callback_count, completion](
            const std::shared_ptr<ClientPluginBaseEvent>&) {
            callback_count->fetch_add(1);
            if (const auto locked = weak_plugin.lock()) {
                locked->RegisterEventCallback(nullptr);
            }
            completion->set_value();
        });

    plugin->CallbackEvent(std::make_shared<ClientPluginBaseEvent>());
    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    plugin->CallbackEvent(std::make_shared<ClientPluginBaseEvent>());
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(callback_count->load(), 1);
    EXPECT_TRUE(plugin->OnDestroy());
}

TEST(ClientPluginEventLifecycle, CallbackCanStopPlugin) {
    const auto plugin = std::make_shared<TestClientPlugin>();
    plugin->StartWithoutUiForTest();
    const auto weak_plugin = std::weak_ptr<TestClientPlugin>(plugin);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    plugin->RegisterEventCallback(
        [weak_plugin, completion](
            const std::shared_ptr<ClientPluginBaseEvent>&) {
            if (const auto locked = weak_plugin.lock()) {
                locked->OnStop();
            }
            completion->set_value();
        });

    plugin->CallbackEvent(std::make_shared<ClientPluginBaseEvent>());
    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(plugin->IsStoppingOrDestroyed());
    EXPECT_TRUE(plugin->OnDestroy());
}

TEST(ClientPluginEventLifecycle, RepeatedCreateDeliverStopDestroyTenRounds) {
    for (int round = 0; round < 10; ++round) {
        const auto plugin = std::make_shared<TestClientPlugin>();
        plugin->StartWithoutUiForTest();
        const auto callback_count = std::make_shared<std::atomic_int>(0);
        const auto completion = std::make_shared<std::promise<void>>();
        auto completed = completion->get_future();
        plugin->RegisterEventCallback(
            [callback_count, completion](
                const std::shared_ptr<ClientPluginBaseEvent>&) {
                callback_count->fetch_add(1);
                completion->set_value();
            });
        plugin->CallbackEvent(std::make_shared<ClientPluginBaseEvent>());
        ASSERT_EQ(completed.wait_for(5s), std::future_status::ready)
            << "round " << round;
        EXPECT_EQ(callback_count->load(), 1) << "round " << round;
        EXPECT_TRUE(plugin->OnStop()) << "round " << round;
        EXPECT_TRUE(plugin->OnDestroy()) << "round " << round;
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QCoreApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
