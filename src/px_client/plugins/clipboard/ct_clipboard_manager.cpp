//
// Created by RGAA on 16/08/2024.
//

#include "ct_clipboard_manager.h"
#include "px_client/ct_client_context.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_message.pb.h"
#include "win/win_message_loop.h"
#include "win/cp_virtual_file.h"
#include "clipboard_plugin.h"
#include "px_client/plugin_interface/ct_plugin_context.h"
#include "px_client/plugin_interface/ct_plugin_events.h"

namespace px
{

    ClipboardManager::ClipboardManager(ClientClipboardPlugin* plugin) : QObject(nullptr) {
        plugin_ = plugin;
        context_ = plugin->GetPluginContext();
        clipboard_platform_ = clipboard::CreatePlatform();
    }

    void ClipboardManager::Start() {
        msg_loop_ = WinMessageLoop::Make(plugin_);
        msg_loop_->Start();
    }

    void ClipboardManager::Stop() {
        if (clipboard_platform_) {
            clipboard_platform_->Clear();
        }
        if (msg_loop_) {
            msg_loop_->Stop();
        }
    }

    void ClipboardManager::OnLocalClipboardUpdated() {
        if (!plugin_->IsClipboardEnabled() || !clipboard_platform_) {
            LOGI("OnLocalClipboardUpdated skipped: enabled={}, platform={}",
                 plugin_->IsClipboardEnabled(), clipboard_platform_ != nullptr);
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
            auto event = std::make_shared<ClientPluginClipboardEvent>();
            event->type_ = ClipboardType::kClipboardFiles;
            for (const auto& file : content.files_) {
                ClipboardFile cf;
                cf.set_full_path(file.full_path_);
                cf.set_file_name(file.file_name_);
                cf.set_ref_path(file.ref_path_);
                cf.set_total_size(file.total_size_);
                event->cp_files_.push_back(cf);
            }
            plugin_->CallbackEvent(event);
            return;
        }

        if (content.HasText()) {
            if (echo_filter_.ShouldSkipOutbound(content.text_)) {
                LOGI("Same with remote, ignore.");
                return;
            }
            LOGI("===> new Text: {}", content.text_);
            auto event = std::make_shared<ClientPluginClipboardEvent>();
            event->type_ = ClipboardType::kClipboardText;
            event->text_msg_ = content.text_;
            plugin_->CallbackEvent(event);
            return;
        }

        LOGI("OnLocalClipboardUpdated: clipboard has no syncable text or files");
    }

    void ClipboardManager::OnRemoteClipboardMessage(std::shared_ptr<px::Message> msg) {
        if (!plugin_->IsClipboardEnabled() || !clipboard_platform_) {
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
            auto event = std::make_shared<ClientPluginRemoteClipboardResp>();
            event->content_type_ = 0;
            event->remote_info_ = in_text;
            plugin_->CallbackEvent(event);
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
                        IID_IDataObject, (void **) &self->data_object_, self->plugin_);
                }
                if (!self->data_object_) {
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
                    auto hr = ::OleSetClipboard(self->data_object_);
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
        if (!plugin_->IsClipboardEnabled()) {
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
