#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "render_panel/companion/console/auth_manager.h"
#include "render_panel/companion/console/console_setting.h"

namespace {

TEST(PanelAuthManagerLifecycle, QueuedRefreshDoesNotRetainDestroyedManagerTenRounds) {
    px::ConsoleSettings::Instance()->UpdateServerConfig("", 0, true);
    for (int round = 1; round <= 10; ++round) {
        auto manager = std::make_shared<px::AuthManager>(nullptr);
        const std::weak_ptr<px::AuthManager> weak_manager = manager;
        auto task = px::MakeAuthRefreshTask(manager);
        manager.reset();

        EXPECT_TRUE(weak_manager.expired()) << "round=" << round;
        EXPECT_NO_THROW(task()) << "round=" << round;
    }
}

TEST(PanelAuthManagerLifecycle, DestroyBeforeConcurrentQueuedRefreshTenRounds) {
    px::ConsoleSettings::Instance()->UpdateServerConfig("", 0, true);
    for (int round = 1; round <= 10; ++round) {
        auto manager = std::make_shared<px::AuthManager>(nullptr);
        const std::weak_ptr<px::AuthManager> weak_manager = manager;
        auto task = px::MakeAuthRefreshTask(manager);
        auto release_task = std::make_shared<std::atomic_bool>(false);
        std::jthread worker([task = std::move(task), release_task]() {
            while (!release_task->load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            task();
        });

        manager.reset();
        EXPECT_TRUE(weak_manager.expired()) << "round=" << round;
        release_task->store(true, std::memory_order_release);
        worker.join();
    }
}

}  // namespace
