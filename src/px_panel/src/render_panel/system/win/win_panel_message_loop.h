#pragma once
#include <Windows.h>
#include <memory>
#include <atomic>
#include "px_common_new/clipboard/clipboard_echo.h"
#include "px_common_new/clipboard/clipboard_platform.h"

namespace px
{

    class PxContext;
    class PxApplication;
    class WinMessageWindow;
    class MessageListener;
    class Thread;

    class WinMessageLoop : public std::enable_shared_from_this<WinMessageLoop> {
    public:
        explicit WinMessageLoop(const std::shared_ptr<PxApplication>& ctx);
        ~WinMessageLoop();
        void Start();
        void Stop();

        void OnClipboardUpdate();
        void SetRemoteClipboardEcho(const std::string& text);
        void BeginSuppressOutboundClipboard();
        void EndSuppressOutboundClipboard();
        clipboard::EchoFilter& GetEchoFilter() { return echo_filter_; }
    private:
        void ProcessLocalClipboardUpdate();
        void CreateMessageWindow();
        static void ThreadFunc(const std::shared_ptr<WinMessageWindow>& message_window);
        void OnWinSessionChange(uint32_t msg);
        static void CALLBACK WinEventProc(
            HWINEVENTHOOK event_hook, DWORD event, HWND window, LONG object_id,
            LONG child_id, DWORD event_thread,
            DWORD event_time); // NOLINT(gammaray-raw-pointer-boundary): Win32 callback ABI
    private:
        std::shared_ptr<Thread> thread_;
        clipboard::EchoFilter echo_filter_;
        std::unique_ptr<clipboard::IPlatform> clipboard_platform_;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<WinMessageWindow> message_window_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::atomic_bool started_ = false;
        std::atomic_bool stopped_ = false;
    };

}
