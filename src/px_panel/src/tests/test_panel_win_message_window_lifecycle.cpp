#include <gtest/gtest.h>

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "render_panel/system/win/win_panel_message_window.h"

namespace {

std::jthread StartWindowThread(
    const std::shared_ptr<px::WinMessageWindow>& window,
    const std::shared_ptr<std::promise<std::uintptr_t>>& startup,
    int round) {
    return std::jthread([window, startup, round]() {
        if (!window->Create(
                "PxPanel_MessageWindow_Test_" + std::to_string(round))) {
            startup->set_value(0);
            return;
        }
        startup->set_value(reinterpret_cast<std::uintptr_t>(
            window->GetHwnd())); // NOLINT(gammaray-raw-pointer-boundary): transient test HWND boundary

        MSG message{};
        for (;;) {
            const int result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) {
                return;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    });
}

TEST(PanelWinMessageWindowLifecycle, CallbackCanCloseWindowTenRounds) {
    for (int round = 1; round <= 10; ++round) {
        auto callback_count = std::make_shared<std::atomic_int>(0);
        auto close_action = std::make_shared<std::function<void()>>();
        auto window = px::WinMessageWindow::Make(
            [callback_count, close_action]() {
                callback_count->fetch_add(1, std::memory_order_relaxed);
                if (*close_action) {
                    (*close_action)();
                }
            });
        const std::weak_ptr<px::WinMessageWindow> weak_window = window;
        *close_action = [weak_window]() {
            if (const auto locked = weak_window.lock()) {
                static_cast<void>(locked->CloseWindow());
            }
        };

        auto startup = std::make_shared<std::promise<std::uintptr_t>>();
        auto startup_result = startup->get_future();
        auto worker = StartWindowThread(window, startup, round);
        const auto window_value = startup_result.get();
        ASSERT_NE(window_value, 0U) << "round=" << round;

        ASSERT_TRUE(PostMessageW(
            reinterpret_cast<HWND>(window_value), // NOLINT(gammaray-raw-pointer-boundary): transient test HWND boundary
            WM_CLIPBOARDUPDATE,
            0,
            0)) << "round=" << round;
        window.reset();
        worker.join();

        EXPECT_EQ(callback_count->load(std::memory_order_relaxed), 1)
            << "round=" << round;
        EXPECT_TRUE(weak_window.expired()) << "round=" << round;
    }
}

TEST(PanelWinMessageWindowLifecycle, RepeatedCloseIsIdempotentTenRounds) {
    for (int round = 1; round <= 10; ++round) {
        auto window = px::WinMessageWindow::Make([]() {});
        const std::weak_ptr<px::WinMessageWindow> weak_window = window;
        auto startup = std::make_shared<std::promise<std::uintptr_t>>();
        auto startup_result = startup->get_future();
        auto worker = StartWindowThread(window, startup, round + 100);
        ASSERT_NE(startup_result.get(), 0U) << "round=" << round;

        for (int close = 0; close < 5; ++close) {
            static_cast<void>(window->CloseWindow());
        }
        window.reset();
        worker.join();

        EXPECT_TRUE(weak_window.expired()) << "round=" << round;
    }
}

}  // namespace
