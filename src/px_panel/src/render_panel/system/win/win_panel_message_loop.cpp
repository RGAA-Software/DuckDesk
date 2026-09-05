#include "win_panel_message_loop.h"
#include <iostream>
#include <type_traits>
#include <wtsapi32.h>
#include "px_common/log.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "win_panel_message_window.h"
#include "px_render_panel_message.pb.h"
#include "px_message/rp_proto_converter.h"
#include "px_common/thread.h"

using namespace pxrp;

namespace
{
    struct WinEventHookReleaser {
        void operator()(
            std::remove_pointer_t<HWINEVENTHOOK>* hook) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): typed Win32 hook RAII boundary
            if (hook) {
                UnhookWinEvent(hook);
            }
        }
    };

    using ScopedWinEventHook = std::unique_ptr<
        std::remove_pointer_t<HWINEVENTHOOK>, WinEventHookReleaser>;
}

namespace px
{

    constexpr char kWindowName[] = "PxPanel_MessageWindow";


    void CALLBACK WinMessageLoop::WinEventProc(
        HWINEVENTHOOK event_hook, DWORD event, HWND window, LONG object_id,
        LONG child_id, DWORD event_thread,
        DWORD event_time) // NOLINT(gammaray-raw-pointer-boundary): Win32 callback ABI
    {
        if (event == EVENT_SYSTEM_DESKTOPSWITCH)
        {
            std::cout << "Desktop switch event detected." << std::endl;
        }
    }

    WinMessageLoop::WinMessageLoop(const std::shared_ptr<PxApplication>& app) {
        app_ = app;
        context_ = app_->GetContext();
        clipboard_platform_ = clipboard::CreatePlatform();
    }

    void WinMessageLoop::SetRemoteClipboardEcho(const std::string& text) {
        echo_filter_.SetRemoteEcho(text);
    }

    void WinMessageLoop::BeginSuppressOutboundClipboard() {
        echo_filter_.BeginSuppressOutbound();
    }

    void WinMessageLoop::EndSuppressOutboundClipboard() {
        echo_filter_.EndSuppressOutbound();
    }

    WinMessageLoop::~WinMessageLoop() {
        Stop();
    }

    void WinMessageLoop::CreateMessageWindow() {
        const auto weak_self = weak_from_this();
        message_window_ = WinMessageWindow::Make([weak_self]() {
            if (const auto self = weak_self.lock(); self && !self->stopped_) {
                self->OnClipboardUpdate();
            }
        });
    }

    void WinMessageLoop::OnClipboardUpdate() {
        // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
#if 0
        if (!app_->IsRendererConnected()) {
            return;
        }
        ProcessLocalClipboardUpdate();
#endif
    }

    void WinMessageLoop::ProcessLocalClipboardUpdate() {
        // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
#if 0
        if (!app_->IsRendererConnected() || !clipboard_platform_) {
            return;
        }
        if (echo_filter_.IsOutboundSuppressed()) {
            return;
        }

        clipboard::Content content;
        if (!clipboard_platform_->Read(content)) {
            LOGE("Read local clipboard failed");
            return;
        }

        auto fn_send_text = [=, this](const std::string& text) {
            if (echo_filter_.ShouldSkipOutbound(text)) {
                return;
            }
            LOGI("===> new Text: {}", text);

            pxrp::RpMessage msg;
            msg.set_type(RpMessageType::kRpClipboardEvent);
            auto sub = msg.mutable_clipboard_info();
            sub->set_type(RpClipboardType::kRpClipboardText);
            sub->set_msg(text);
            app_->PostMessage2Renderer(px::RpProtoAsData(&msg));
        };

        if (content.HasFiles()) {
            pxrp::RpMessage msg;
            msg.set_type(RpMessageType::kRpClipboardEvent);
            auto sub = msg.mutable_clipboard_info();
            sub->set_type(RpClipboardType::kRpClipboardFiles);
            for (const auto& file : content.files_) {
                auto target_file = sub->mutable_files()->Add();
                target_file->set_full_path(file.full_path_);
                target_file->set_file_name(file.file_name_);
                target_file->set_ref_path(file.ref_path_);
                target_file->set_total_size(file.total_size_);
            }
            app_->PostMessage2Renderer(px::RpProtoAsData(&msg));
        } else if (content.HasText()) {
            fn_send_text(content.text_);
        }
#endif
    }

    void WinMessageLoop::OnWinSessionChange(uint32_t message) {

    }

    void WinMessageLoop::Start() {
        if (started_.exchange(true) || stopped_) {
            return;
        }
        msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kControl);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgRemoteClipboardResp>([weak_self](const MsgRemoteClipboardResp& msg) {
            if (const auto self = weak_self.lock(); self && !self->stopped_) {
                self->SetRemoteClipboardEcho(msg.text_msg_);
                LOGI("===> Remote is :{}", msg.text_msg_);
            }
        });
        CreateMessageWindow();
        const auto message_window = message_window_;
        thread_ = Thread::MakeOnceTask([message_window]() {
            ThreadFunc(message_window);
        }, "panel_win_message_loop", false);
    }

    void WinMessageLoop::Stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        LOGI("WinMessageLoop stopping...");
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (clipboard_platform_) {
            clipboard_platform_->Clear();
        }
        if (message_window_) {
            const bool close_posted = message_window_->CloseWindow();
            if (!close_posted && thread_ && thread_->GetTid() != 0) {
                static_cast<void>(PostThreadMessageW(
                    thread_->GetTid(), WM_QUIT, 0, 0));
            }
        }
        if (thread_) {
            thread_->Exit();
            thread_.reset();
        }
        message_window_.reset();
        context_.reset();
        app_.reset();
        LOGI("WinMessageLoop stoped.");
    }

    void WinMessageLoop::ThreadFunc(const std::shared_ptr<WinMessageWindow>& message_window) {
        if (!message_window || !message_window->Create(kWindowName)) {
            LOGE("WinMessageLoop create window error.");
            return;
        }
        LOGI("WinMessageWindow create success");
        const auto window_value = reinterpret_cast<std::uintptr_t>(
            message_window->GetHwnd()); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        if (window_value == 0) {
            LOGE("WinMessageLoop hwnd is nullptr.");
            return;
        }

        AddClipboardFormatListener(
            reinterpret_cast<HWND>(window_value)); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        LOGI("AddClipboardFormatListener already add WinMessageWindow");

        if (!WTSRegisterSessionNotification(
                reinterpret_cast<HWND>(window_value), // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
                NOTIFY_FOR_ALL_SESSIONS)) {
            LOGE("WTSRegisterSessionNotification error: %d", GetLastError());
        }

        ScopedWinEventHook event_hook(SetWinEventHook(
            EVENT_SYSTEM_DESKTOPSWITCH,
            EVENT_SYSTEM_DESKTOPSWITCH,
            nullptr,
            &WinMessageLoop::WinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT));

        if (!event_hook)
        {
            std::cout << "Failed to set event hook." << std::endl;
        }

        int bRet = 0;
        MSG msg{};
        while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
            if (bRet == -1) {
                break;
            }
            else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        WTSUnRegisterSessionNotification(
            reinterpret_cast<HWND>(window_value)); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        RemoveClipboardFormatListener(
            reinterpret_cast<HWND>(window_value)); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HWND boundary
        if (message_window->GetHwnd()) {
            DestroyWindow(
                message_window->GetHwnd()); // NOLINT(gammaray-raw-pointer-boundary): destroy residual window on its creating thread
        }
    }

}
