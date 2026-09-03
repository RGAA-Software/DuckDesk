#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <QCoreApplication>
#include <gtest/gtest.h>

#include "px_client/modules/client_module_context.h"

namespace px {
namespace {

using namespace std::chrono_literals;

TEST(ClientModuleContextLifecycle, StopFromTimerCallbackDoesNotSelfJoin) {
    const auto context =
        std::make_shared<ClientModuleContext>("client-module-timer-stop");
    const auto weak_context = std::weak_ptr<ClientModuleContext>(context);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    context->StartTimer(1, [weak_context, completion]() {
        if (const auto locked = weak_context.lock()) {
            locked->Stop();
        }
        completion->set_value();
    });

    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    context->Stop();
}

TEST(ClientModuleContextLifecycle, StopFromWorkCallbackDoesNotSelfJoin) {
    const auto context =
        std::make_shared<ClientModuleContext>("client-module-worker-stop");
    const auto weak_context = std::weak_ptr<ClientModuleContext>(context);
    const auto completion = std::make_shared<std::promise<void>>();
    auto completed = completion->get_future();
    context->PostWorkTask([weak_context, completion]() {
        if (const auto locked = weak_context.lock()) {
            locked->Stop();
        }
        completion->set_value();
    });

    ASSERT_EQ(completed.wait_for(5s), std::future_status::ready);
    context->Stop();
}

TEST(ClientModuleContextLifecycle, StopCancelsDelayedCallbacks) {
    const auto context =
        std::make_shared<ClientModuleContext>("client-module-delay-cancel");
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    for (int index = 0; index < 64; ++index) {
        context->PostDelayTask(
            [callback_count]() { ++*callback_count; }, 200);
    }

    context->Stop();
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(callback_count->load(), 0);
}

TEST(ClientModuleContextLifecycle, StopCancelsQueuedUiCallbacks) {
    auto context =
        std::make_shared<ClientModuleContext>("client-module-ui-cancel");
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    std::jthread producer([context, callback_count]() {
        for (int index = 0; index < 64; ++index) {
            context->PostUITask([callback_count]() { ++*callback_count; });
        }
    });
    producer.join();

    context->Stop();
    context.reset();
    QCoreApplication::processEvents();
    EXPECT_EQ(callback_count->load(), 0);
}

TEST(ClientModuleContextLifecycle, ConcurrentPostAndStopIsSafe) {
    for (int round = 0; round < 10; ++round) {
        const auto context = std::make_shared<ClientModuleContext>(
            "client-module-concurrent-stop");
        const auto callback_count = std::make_shared<std::atomic_int>(0);
        std::jthread producer([context, callback_count]() {
            for (int index = 0; index < 256; ++index) {
                context->PostWorkTask(
                    [callback_count]() { ++*callback_count; });
                context->PostDelayTask(
                    [callback_count]() { ++*callback_count; }, 100);
            }
        });
        context->Stop();
        producer.join();
        context->Stop();
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QCoreApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
