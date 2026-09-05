//
// Created by RGAA on 16/08/2024.
//

#include "ct_clipboard_manager.h"
#include "px_client/ct_client_context.h"
#include "px_common/log.h"
#include "px_common/time_util.h"
#include "px_message.pb.h"
#include "win/win_message_loop.h"
#include "win/cp_virtual_file.h"
#include "clipboard_runtime_bridge.h"

#include <utility>
#include <vector>

namespace px
{

    ClipboardManager::ClipboardManager(
        std::shared_ptr<ClipboardRuntimeBridge> runtime_bridge)
        : QObject(nullptr), runtime_bridge_(std::move(runtime_bridge)) {
        clipboard_platform_ = clipboard::CreatePlatform();
    }

    bool ClipboardManager::Start() {
        if (msg_loop_) {
            return true;
        }
        const auto weak_self = weak_from_this();
        msg_loop_ = WinMessageLoop::Make([weak_self]() {
            if (const auto self = weak_self.lock()) {
                self->OnLocalClipboardUpdated();
            }
        });
        if (!msg_loop_->Start()) {
            msg_loop_.reset();
            return false;
        }
        return true;
    }

    void ClipboardManager::Stop() {
        if (msg_loop_) {
            msg_loop_->Stop();
            msg_loop_.reset();
        }
        if (clipboard_platform_) {
            clipboard_platform_->Clear();
        }
        virtual_file_.Reset();
    }

    void ClipboardManager::OnLocalClipboardUpdated() {
        if (!runtime_bridge_ || !runtime_bridge_->IsEnabled() ||
            !clipboard_platform_) {
            LOGI("OnLocalClipboardUpdated skipped: enabled={}, platform={}",
                 runtime_bridge_ && runtime_bridge_->IsEnabled(),
                 clipboard_platform_ != nullptr);
            return;
        }
        if (echo_filter_.IsOutboundSuppressed()) {
            LOGI("OnLocalClipboardUpdated skipped: outbound suppressed");
            return;
        }

        clipboard::Content content;
        if (!clipboard_platform_->Read(content)) {
            LOGE("Read local clipboard failed");
            return;
        }

        if (content.HasFiles()) {
            std::vector<ClipboardFile> files;
            for (const auto& file : content.files_) {
                ClipboardFile cf;
                cf.set_full_path(file.full_path_);
                cf.set_file_name(file.file_name_);
                cf.set_ref_path(file.ref_path_);
                cf.set_total_size(file.total_size_);
                files.emplace_back(std::move(cf));
            }
            runtime_bridge_->SendClipboardUpdate(
                ClipboardType::kClipboardFiles, {}, std::move(files));
            return;
        }

        if (content.HasText()) {
            if (echo_filter_.ShouldSkipOutbound(content.text_)) {
                LOGI("Same with remote, ignore.");
                return;
            }
            LOGI("===> new Text: {}", content.text_);
            runtime_bridge_->SendClipboardUpdate(
                ClipboardType::kClipboardText, content.text_, {});
            return;
        }

        LOGI("OnLocalClipboardUpdated: clipboard has no syncable text or files");
    }

    void ClipboardManager::OnRemoteClipboardMessage(std::shared_ptr<px::Message> msg) {
        if (!runtime_bridge_ || !runtime_bridge_->IsEnabled() ||
            !clipboard_platform_) {
            return;
        }

        if (msg->type() != MessageType::kClipboardInfo) {
            return;
        }

        auto info = msg->clipboard_info();
        if (info.type() == ClipboardType::kClipboardText) {
            const auto& in_text = info.msg();
            if (in_text.empty()) {
                return;
            }

            clipboard::SuppressOutboundGuard suppress_guard(echo_filter_);
            echo_filter_.SetRemoteEcho(in_text);

            if (!clipboard::WriteTextWithRetry(*clipboard_platform_, in_text)) {
                LOGE("Failed to apply remote clipboard text after retries");
                return;
            }

            LOGI("*** update remote clipboard info: {}", in_text);
            runtime_bridge_->SendRemoteClipboardResponse(in_text);
        }
        else if (info.type() == ClipboardType::kClipboardFiles) {
            const auto &files = info.files();
            std::vector<ClipboardFile> target_files;
            for (auto &file: files) {
                ClipboardFile cpy_file;
                cpy_file.CopyFrom(file);
                target_files.push_back(file);
            }

            if (!msg_loop_) {
                LOGE("Message loop is null!");
                return;
            }

            // OleSetClipboard must run on an STA thread with a message pump so the
            // data object can be marshaled cross-process (Explorer queries it when
            // deciding whether to enable "Paste"). Run the whole clipboard write on
            // the clipboard message-loop thread.
            const auto weak_self = weak_from_this();
            msg_loop_->PostTask([weak_self, target_files]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                clipboard::SuppressOutboundGuard suppress_guard(self->echo_filter_);

                if (!self->virtual_file_) {
                    self->virtual_file_ = px::CreateVirtualFile(
                        self->runtime_bridge_);
                }
                if (!self->virtual_file_) {
                    LOGE("DataObject is null!");
                    return;
                }

                bool cleared_clipboard = false;
                for (int i = 0; i < 20; i++) {
                    if (self->clipboard_platform_->Clear()) {
                        cleared_clipboard = true;
                        break;
                    }
                    TimeUtil::DelayBySleep(10);
                }
                if (!cleared_clipboard) {
                    LOGE("Empty clipboard failed!");
                    return;
                }

                TimeUtil::DelayBySleep(10);

                bool set_clipboard = false;
                for (int i = 0; i < 20; i++) {
                    auto hr = ::OleSetClipboard(self->virtual_file_.Get());
                    if (hr == S_OK) {
                        set_clipboard = true;
                        break;
                    }
                    TimeUtil::DelayBySleep(10);
                }
                if (!set_clipboard) {
                    LOGE("Set clipboard failed!");
                    return;
                }

                self->virtual_file_->OnClipboardFilesInfo(target_files);
            });
        }
    }

    void ClipboardManager::OnRemoteClipboardRespMessage(std::shared_ptr<px::Message> msg) {
        if (!runtime_bridge_ || !runtime_bridge_->IsEnabled()) {
            return;
        }
        if (msg->type() != MessageType::kClipboardInfoResp) {
            return;
        }
        auto sub = msg->clipboard_info_resp();
        if (sub.type() == ClipboardType::kClipboardText) {
            echo_filter_.SetRemoteEcho(sub.msg());
            LOGI("CBK ===> remote clipboard text resp: {}", sub.msg());
        }
    }

    void ClipboardManager::OnRemoteFileRespMessage(std::shared_ptr<px::Message> msg) {
        if (virtual_file_) {
            virtual_file_->OnClipboardRespBuffer(msg->cp_resp_buffer());
        }
    }
}
