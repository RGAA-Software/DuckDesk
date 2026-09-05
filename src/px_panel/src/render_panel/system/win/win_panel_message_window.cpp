#include "win_panel_message_window.h"

#include <iostream>
#include <utility>

#include "px_common/log.h"

namespace px
{
    namespace
    {
        constexpr char kWindowClassName[] = "PxPanel_MessageWindowClass";
    }

    struct WinMessageWindow::CallbackBridge {
        std::weak_ptr<WinMessageWindow> owner_;
    };

    std::atomic<int> WinMessageWindow::current_create_window_count_ = 0;
    std::string WinMessageWindow::class_name_;
    std::atomic<bool> WinMessageWindow::class_registered_ = false;
    std::mutex WinMessageWindow::register_mutex_;

    std::shared_ptr<WinMessageWindow> WinMessageWindow::Make(
        ClipboardUpdatedCallback clipboard_updated_callback) {
        return std::make_shared<WinMessageWindow>(
            std::move(clipboard_updated_callback));
    }

    WinMessageWindow::WinMessageWindow(
        ClipboardUpdatedCallback clipboard_updated_callback)
        : clipboard_updated_callback_(std::move(clipboard_updated_callback)),
          callback_bridge_(std::make_shared<CallbackBridge>()) {
    }

    WinMessageWindow::~WinMessageWindow() {
        static_cast<void>(CloseWindow());
        UnregisterWindowClass();
    }

    std::shared_ptr<WinMessageWindow> WinMessageWindow::LockOwner(
        LONG_PTR bridge_value) {
        if (bridge_value == 0) {
            return nullptr;
        }
        return reinterpret_cast<CallbackBridge*>(bridge_value) // NOLINT(gammaray-raw-pointer-boundary): Win32 user-data boundary
            ->owner_.lock();
    }

    std::shared_ptr<WinMessageWindow> WinMessageWindow::LockOwnerFromCreate(
        LPARAM create_value) {
        const auto bridge_value = reinterpret_cast<LONG_PTR>(
            reinterpret_cast<LPCREATESTRUCT>(create_value) // NOLINT(gammaray-raw-pointer-boundary): Win32 create-message boundary
                ->lpCreateParams);
        return LockOwner(bridge_value);
    }

    LRESULT CALLBACK WinMessageWindow::WindowProc(
        HWND window, UINT msg, WPARAM w_param, LPARAM l_param) { // NOLINT(gammaray-raw-pointer-boundary): Win32 callback ABI
        auto self = LockOwner(GetWindowLongPtrW(window, GWLP_USERDATA));

        switch (msg) {
        case WM_CREATE: {
            self = LockOwnerFromCreate(l_param);
            if (!self) {
                return -1;
            }
            if (self->StoreWindow(window)) {
                ++current_create_window_count_;
            }
            SetLastError(ERROR_SUCCESS);
            static_cast<void>(SetWindowLongPtrA(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self->callback_bridge_.get()))); // NOLINT(gammaray-raw-pointer-boundary): bridge is retained until WM_DESTROY
            break;
        }
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            SetLastError(ERROR_SUCCESS);
            static_cast<void>(SetWindowLongPtrA(window, GWLP_USERDATA, 0));
            if (self && self->ClearWindow()) {
                --current_create_window_count_;
            }
            PostQuitMessage(0);
            return 0;

        case WM_CLIPBOARDUPDATE:
            if (self) {
                self->OnClipboardUpdate();
            }
            return 0;

        case WM_WTSSESSION_CHANGE:
            LOGI("Panel session state changed: {}", w_param);
            return 0;

        case WM_DISPLAYCHANGE:
            return 0;

        default:
            break;
        }
        return DefWindowProcA(window, msg, w_param, l_param);
    }

    bool WinMessageWindow::RegisterWindowClass(
        HINSTANCE instance) { // NOLINT(gammaray-raw-pointer-boundary): transient Win32 module handle
        std::lock_guard lock(register_mutex_);
        if (class_registered_) {
            return true;
        }

        WNDCLASSEXA window_class{};
        window_class.cbSize = sizeof(window_class);
        static std::once_flag flag;
        std::call_once(flag, []() {
            class_name_ = std::string(kWindowClassName) + "_" +
                std::to_string(GetCurrentProcessId());
        });
        window_class.lpszClassName = class_name_.c_str();
        window_class.hInstance = instance;
        window_class.lpfnWndProc = WindowProc;

        if (!RegisterClassExA(&window_class)) {
            std::cout << "RegisterClassExA failed GetLastError = "
                      << GetLastError() << std::endl;
            return false;
        }

        class_registered_ = true;
        return true;
    }

    void WinMessageWindow::UnregisterWindowClass() {
        std::lock_guard lock(register_mutex_);
        if (!class_registered_ || current_create_window_count_ != 0) {
            return;
        }
        HINSTANCE instance = nullptr; // NOLINT(gammaray-raw-pointer-boundary): transient Win32 module handle
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<char*>(&WindowProc), // NOLINT(gammaray-raw-pointer-boundary): Win32 module lookup boundary
                &instance)) {
            return;
        }
        if (UnregisterClassA(class_name_.c_str(), instance)) {
            class_registered_ = false;
        }
    }

    bool WinMessageWindow::Create(const std::string& window_name) {
        if (GetHwnd()) {
            return true;
        }

        HINSTANCE instance = nullptr; // NOLINT(gammaray-raw-pointer-boundary): transient Win32 module handle
        window_name_ = window_name;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<char*>(&WindowProc), // NOLINT(gammaray-raw-pointer-boundary): Win32 module lookup boundary
                &instance)) {
            LOGE("GetModuleHandleExA failed: {}", GetLastError());
            return false;
        }

        if (!RegisterWindowClass(instance)) {
            return false;
        }

        close_requested_.store(false, std::memory_order_release);
        callback_bridge_->owner_ = weak_from_this();
        static_cast<void>(CreateWindowA(
            class_name_.c_str(),
            window_name_.c_str(),
            0,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance,
            callback_bridge_.get())); // NOLINT(gammaray-raw-pointer-boundary): Win32 retains bridge only for the window lifetime

        if (!GetHwnd()) {
            callback_bridge_->owner_.reset();
            LOGE("CreateWindowA failed: {}", GetLastError());
            return false;
        }
        return true;
    }

    bool WinMessageWindow::StoreWindow(
        HWND window) { // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        std::uintptr_t expected = 0;
        return window_handle_.compare_exchange_strong(
            expected,
            reinterpret_cast<std::uintptr_t>(window),
            std::memory_order_acq_rel);
    }

    bool WinMessageWindow::ClearWindow() {
        callback_bridge_->owner_.reset();
        return window_handle_.exchange(0, std::memory_order_acq_rel) != 0;
    }

    HWND WinMessageWindow::GetHwnd() const { // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        return reinterpret_cast<HWND>(
            window_handle_.load(std::memory_order_acquire));
    }

    bool WinMessageWindow::CloseWindow() {
        const auto window_value = window_handle_.load(std::memory_order_acquire);
        if (window_value == 0 || close_requested_.exchange(true)) {
            return true;
        }
        if (PostMessageA(
                reinterpret_cast<HWND>(window_value), // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
                WM_CLOSE,
                0,
                0)) {
            return true;
        }
        close_requested_.store(false, std::memory_order_release);
        return false;
    }

    void WinMessageWindow::OnClipboardUpdate() {
        if (clipboard_updated_callback_) {
            clipboard_updated_callback_();
        }
    }
}
