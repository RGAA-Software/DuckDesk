#pragma once

#include <Windows.h>
#include <functional>
#include <atomic>
#include <mutex>

namespace px
{

    using MessageCallback = std::function<bool(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result)>;

    class WinMessageWindow {
    public:
        using ClipboardUpdatedCallback = std::function<void()>;

        static std::shared_ptr<WinMessageWindow> Make(
            ClipboardUpdatedCallback clipboard_updated_callback);
        explicit WinMessageWindow(
            ClipboardUpdatedCallback clipboard_updated_callback);
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
        static void UnregisterWindowClass();
        static LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam);

        /*剪切板更新*/
        void OnLocalClipboardUpdated(HWND hwnd);

        /*显示设备变化消息*/
        void OnDisplayChange();

        void RunPostedTask();

    private:
        ClipboardUpdatedCallback clipboard_updated_callback_;
        MessageCallback message_callback_;
        HWND mHwnd = nullptr;
        std::string window_name_;

        std::mutex task_mutex_;
        std::function<void()> pending_task_;

        static std::mutex register_mutex_;
        static std::atomic<int> current_create_window_count_;
        static std::string class_name_;
        static std::atomic<bool> class_registered_;
        std::atomic_bool close_requested_ = false;
    };
}
