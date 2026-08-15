#pragma once

#include <Windows.h>
#include <functional>
#include <atomic>
#include <mutex>

namespace px
{

    class WinMessageLoop;
    class ClientClipboardPlugin;

    using MessageCallback = std::function<bool(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result)>;

    class WinMessageWindow {
    public:
        static std::shared_ptr<WinMessageWindow> Make(ClientClipboardPlugin* plugin, std::shared_ptr<WinMessageLoop> message_loop);
        explicit WinMessageWindow(ClientClipboardPlugin* plugin, std::shared_ptr<WinMessageLoop> message_loop);
        ~WinMessageWindow();
        bool Create(const std::string& window_name);
        HWND GetHwnd() const;
        void CloseWindow();

        // Run |task| on the message-loop thread (the clipboard STA thread with a
        // message pump). Used so OleSetClipboard executes on an STA that can marshal
        // the data object cross-process (Explorer queries it on paste).
        void PostTask(std::function<void()> task);

    private:
        static bool registerWindowClass(HINSTANCE instance);
        static LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam);

        /*剪切板更新*/
        void OnLocalClipboardUpdated(HWND hwnd);

        /*显示设备变化消息*/
        void OnDisplayChange();

        void RunPostedTask();

    private:
        ClientClipboardPlugin* plugin_ = nullptr;
        MessageCallback message_callback_;
        HWND mHwnd = nullptr;
        std::string window_name_;

        std::weak_ptr<WinMessageLoop> message_loop_;

        std::mutex task_mutex_;
        std::function<void()> pending_task_;

        static std::mutex register_mutex_;
        static std::atomic<int> current_create_window_count_;
        static std::string class_name_;
        static std::atomic<bool> class_registered_;
    };
}