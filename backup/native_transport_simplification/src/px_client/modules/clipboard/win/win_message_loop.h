#pragma once
#include <Windows.h>
#include <memory>
#include <thread>
#include <functional>
#include <future>

namespace px
{

    class WinMessageWindow;

    // 监听windows消息
    class WinMessageLoop : public std::enable_shared_from_this<WinMessageLoop> {
    public:
        using ClipboardUpdatedCallback = std::function<void()>;

        static std::shared_ptr<WinMessageLoop> Make(
            ClipboardUpdatedCallback clipboard_updated_callback);
        explicit WinMessageLoop(
            ClipboardUpdatedCallback clipboard_updated_callback);
        ~WinMessageLoop();
        [[nodiscard]] bool Start();
        void Stop();

        // Run |task| on the message-loop thread (clipboard STA + message pump).
        void PostTask(std::function<void()> task);

        void OnDisplayDeviceChange();
    private:
        void CreateMessageWindow();
        static void ThreadFunc(
            const std::shared_ptr<WinMessageWindow>& message_window,
            const std::shared_ptr<std::promise<bool>>& startup_signal);
        void OnWinSessionChange(uint32_t msg);
        static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
    private:
        ClipboardUpdatedCallback clipboard_updated_callback_;
        std::jthread thread_;
        std::shared_ptr<WinMessageWindow> message_window_ = nullptr;
    };

}
