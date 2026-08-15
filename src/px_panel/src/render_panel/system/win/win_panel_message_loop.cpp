#include "win_panel_message_loop.h"
#include <iostream>
#include <wtsapi32.h>
#include "px_common_new/log.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "win_panel_message_window.h"
#include "px_render_panel_message.pb.h"
#include "px_message_new/rp_proto_converter.h"

using namespace tcrp;

namespace tc
{

    constexpr char kWindowName[] = "GammaRay_panel_MessageWindow";


    void CALLBACK WinMessageLoop::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
    {
        if (event == EVENT_SYSTEM_DESKTOPSWITCH)
        {
            std::cout << "Desktop switch event detected." << std::endl;
        }
    }

    WinMessageLoop::WinMessageLoop(const std::shared_ptr<GrApplication>& app) {
        app_ = app;
        context_ = app_->GetContext();
        clipboard_platform_ = clipboard::CreatePlatform();
        msg_listener_ = context_->ObtainMessageListener();
        msg_listener_->Listen<MsgRemoteClipboardResp>([=, this](const MsgRemoteClipboardResp& msg) {
            SetRemoteClipboardEcho(msg.text_msg_);
            LOGI("===> Remote is :{}", msg.text_msg_);
        });
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

    }

    void WinMessageLoop::CreateMessageWindow() {
        message_window_ = WinMessageWindow::Make(context_, shared_from_this());
    }

    void WinMessageLoop::OnClipboardUpdate(HWND hwnd) {
        // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
        (void)hwnd;
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

            tcrp::RpMessage msg;
            msg.set_type(RpMessageType::kRpClipboardEvent);
            auto sub = msg.mutable_clipboard_info();
            sub->set_type(RpClipboardType::kRpClipboardText);
            sub->set_msg(text);
            app_->PostMessage2Renderer(tc::RpProtoAsData(&msg));
        };

        if (content.HasFiles()) {
            tcrp::RpMessage msg;
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
            app_->PostMessage2Renderer(tc::RpProtoAsData(&msg));
        } else if (content.HasText()) {
            fn_send_text(content.text_);
        }
#endif
    }

    void WinMessageLoop::OnWinSessionChange(uint32_t message) {

    }

    void WinMessageLoop::Start() {
        CreateMessageWindow();
        thread_ = std::thread(std::bind(&WinMessageLoop::ThreadFunc, this));
    }

    void WinMessageLoop::Stop() {
        LOGI("WinMessageLoop stopping...");
        if (clipboard_platform_) {
            clipboard_platform_->Clear();
        }
        message_window_->CloseWindow();
        if (thread_.joinable()) {
            thread_.join();
        }
        LOGI("WinMessageLoop stoped.");
    }

    void WinMessageLoop::ThreadFunc() {
        if (!message_window_->Create(kWindowName)) {
            LOGE("WinMessageLoop create window error.");
            return;
        }
        LOGI("WinMessageWindow create success");
        HWND hwnd = message_window_->GetHwnd();
        if (!hwnd) {
            LOGE("WinMessageLoop hwnd is nullptr.");
            return;
        }

        AddClipboardFormatListener(hwnd);
        LOGI("AddClipboardFormatListener already add WinMessageWindow");

        if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_ALL_SESSIONS)) {
            LOGE("WTSRegisterSessionNotification error: %d", GetLastError());
        }

        HWINEVENTHOOK hEventHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH, nullptr, &WinMessageLoop::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

        if (hEventHook == nullptr)
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

        RemoveClipboardFormatListener(hwnd);
        if (hEventHook != nullptr) {
            UnhookWinEvent(hEventHook);
        }
    }

}
