#include <Windows.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "render_panel/px_running_pipe.h"

namespace {

using namespace std::chrono_literals;

std::string MakePipeName(int round, std::string_view suffix) {
    return std::format(R"(\\.\pipe\running\panel_lifecycle_{}_{}_{})",
                       GetCurrentProcessId(), round, suffix);
}

bool SendUntilConnected(const std::shared_ptr<px::PxRunningPipe>& sender) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sender->SendHello()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return sender->SendHello();
}

bool WaitFor(const std::shared_ptr<std::atomic<int>>& value, int expected) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (value->load(std::memory_order_acquire) >= expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return value->load(std::memory_order_acquire) >= expected;
}

bool WaitForEmpty(
    const std::shared_ptr<std::atomic<std::shared_ptr<px::PxRunningPipe>>>& owner) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!owner->load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::yield();
    }
    return !owner->load(std::memory_order_acquire);
}

TEST(PanelRunningPipeLifecycle, RealHelloStopAndRepeatTenRounds) {
    for (int round = 1; round <= 10; ++round) {
        const auto name = MakePipeName(round, "repeat");
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto server = std::make_shared<px::PxRunningPipe>(name);
        auto sender = std::make_shared<px::PxRunningPipe>(name);
        server->StartListening([calls]() {
            calls->fetch_add(1, std::memory_order_release);
        });
        ASSERT_TRUE(SendUntilConnected(sender)) << "round=" << round;
        ASSERT_TRUE(WaitFor(calls, 1)) << "round=" << round;

        const auto stop_start = std::chrono::steady_clock::now();
        server->StopListening();
        EXPECT_LT(std::chrono::steady_clock::now() - stop_start, 500ms);

        server->StartListening([calls]() {
            calls->fetch_add(1, std::memory_order_release);
        });
        ASSERT_TRUE(SendUntilConnected(sender)) << "restart round=" << round;
        ASSERT_TRUE(WaitFor(calls, 2)) << "restart round=" << round;
        server.reset();
    }
}

TEST(PanelRunningPipeLifecycle, DestroyLastOwnerFromReceiveCallbackTenRounds) {
    for (int round = 1; round <= 10; ++round) {
        const auto name = MakePipeName(round, "self_destroy");
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto owner = std::make_shared<std::atomic<std::shared_ptr<px::PxRunningPipe>>>(
            std::make_shared<px::PxRunningPipe>(name));
        owner->load()->StartListening([calls, owner]() {
            calls->fetch_add(1, std::memory_order_release);
            owner->store(nullptr, std::memory_order_release);
        });
        auto sender = std::make_shared<px::PxRunningPipe>(name);
        ASSERT_TRUE(SendUntilConnected(sender)) << "round=" << round;
        ASSERT_TRUE(WaitFor(calls, 1)) << "round=" << round;
        EXPECT_TRUE(WaitForEmpty(owner)) << "round=" << round;
    }
}

}  // namespace
