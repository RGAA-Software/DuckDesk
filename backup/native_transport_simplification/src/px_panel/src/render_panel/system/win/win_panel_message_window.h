#pragma once

#include <Windows.h>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <memory>

namespace px
{

    class WinMessageWindow : public std::enable_shared_from_this<WinMessageWindow> {
    public:
        using ClipboardUpdatedCallback = std::function<void()>;

        static std::shared_ptr<WinMessageWindow> Make(
            ClipboardUpdatedCallback clipboard_updated_callback);
        explicit WinMessageWindow(
            ClipboardUpdatedCallback clipboard_updated_callback);
        ~WinMessageWindow();
        bool Create(const std::string& window_name);
        HWND GetHwnd() const; // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        [[nodiscard]] bool CloseWindow();
    private:
        struct CallbackBridge;

        static bool RegisterWindowClass(
            HINSTANCE instance); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 module handle
        static void UnregisterWindowClass();
        static std::shared_ptr<WinMessageWindow> LockOwner(LONG_PTR bridge_value);
        static std::shared_ptr<WinMessageWindow> LockOwnerFromCreate(LPARAM create_value);
        static LRESULT CALLBACK WindowProc(
            HWND window, UINT msg, WPARAM w_param, LPARAM l_param); // NOLINT(gammaray-raw-pointer-boundary): Win32 callback ABI
        bool StoreWindow(
            HWND window); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        bool ClearWindow();
        void OnClipboardUpdate();

    private:
        ClipboardUpdatedCallback clipboard_updated_callback_;
        std::shared_ptr<CallbackBridge> callback_bridge_;
        std::atomic_uintptr_t window_handle_{0};
        std::atomic_bool close_requested_{false};
        std::string window_name_;

        static std::mutex register_mutex_;
        static std::atomic<int> current_create_window_count_;
        static std::string class_name_;
        static std::atomic<bool> class_registered_;
    };
}
