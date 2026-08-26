#pragma once
#include <Windows.h>
#include <memory>
#include <thread>
#include <atomic>
#include "px_common_new/clipboard/clipboard_echo.h"
#include "px_common_new/clipboard/clipboard_platform.h"

namespace px
{

    class PxContext;
    class PxApplication;
    class WinMessageWindow;
    class MessageListener;

    class WinMessageLoop : public std::enable_shared_from_this<WinMessageLoop> {
    public:
        explicit WinMessageLoop(const std::shared_ptr<PxApplication>& ctx);
        ~WinMessageLoop();
        void Start();
        void Stop();

        void OnClipboardUpdate(HWND hwnd);
        void SetRemoteClipboardEcho(const std::string& text);
        void BeginSuppressOutboundClipboard();
        void EndSuppressOutboundClipboard();
        clipboard::EchoFilter& GetEchoFilter() { return echo_filter_; }
    private:
        void ProcessLocalClipboardUpdate();
        void CreateMessageWindow();
        static void ThreadFunc(const std::shared_ptr<WinMessageWindow>& message_window);
        void OnWinSessionChange(uint32_t msg);
        static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
    private:
        std::thread thread_;
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
