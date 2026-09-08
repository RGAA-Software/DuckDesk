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
#include "px_client_sdk/sdk_clipboard_protocol.h"

#include <utility>
#include <vector>
#include <unordered_set>

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
        static_cast<void>(update_epoch_.Advance());
        CancelVirtualFiles();
        if (msg_loop_) {
            msg_loop_->Stop();
            msg_loop_.reset();
        }
        if (clipboard_platform_) {
            clipboard_platform_->Clear();
        }
        CancelVirtualFiles();
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

        Microsoft::WRL::ComPtr<CpVirtualFile> current_offer{};
        {
            std::lock_guard lock(virtual_file_mutex_);
            current_offer = virtual_file_;
        }
        // WM_CLIPBOARDUPDATE can arrive after the synchronous suppression guard has ended.
        if (current_offer && ::OleIsCurrentClipboard(current_offer.Get()) == S_OK) {
            return;
        }
        CancelVirtualFiles();

        clipboard::Content content;
        if (!clipboard_platform_->Read(content)) {
            LOGE("Read local clipboard failed");
            return;
        }

        if (content.HasFiles()) {
            static_cast<void>(update_epoch_.Advance());
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
            static_cast<void>(update_epoch_.Advance());
            runtime_bridge_->SendClipboardUpdate(
                ClipboardType::kClipboardText, content.text_, {});
            return;
        }

        LOGI("OnLocalClipboardUpdated: clipboard has no syncable text or files");
        static_cast<void>(update_epoch_.Advance());
        runtime_bridge_->RevokeLocalFiles();
    }

    void ClipboardManager::OnRemoteClipboardMessage(std::shared_ptr<px::Message> message) {
        if (!message || message->type() != kClipboardInfo || !message->has_clipboard_info() || !runtime_bridge_ || !runtime_bridge_->IsEnabled() ||
            !msg_loop_) {
            return;
        }
        runtime_bridge_->RevokeLocalFiles();
        const auto epoch = update_epoch_.Advance();
        const auto weak_self = weak_from_this();
        msg_loop_->PostTask([weak_self, message = std::move(message), epoch] {
            if (const auto self = weak_self.lock()) {
                self->ApplyRemoteClipboardMessage(message, epoch);
            }
        });
    }

    void ClipboardManager::CancelVirtualFiles() {
        Microsoft::WRL::ComPtr<CpVirtualFile> previous{};
        {
            std::lock_guard lock(virtual_file_mutex_);
            previous.Swap(virtual_file_);
        }
        if (previous) {
            previous->ExitAllStreams();
        }
    }

    void ClipboardManager::ApplyRemoteClipboardMessage(const std::shared_ptr<Message>& message, std::uint64_t epoch) {
        if (!update_epoch_.IsCurrent(epoch) || !runtime_bridge_->IsEnabled() || !clipboard_platform_) {
            return;
        }
        const auto& info = message->clipboard_info();
        CancelVirtualFiles();
        clipboard::SuppressOutboundGuard suppress_guard(echo_filter_);
        if (info.type() == kClipboardText) {
            const auto& text = info.msg();
            if (text.empty() || text.size() > 1'048'576U) {
                return;
            }
            echo_filter_.SetRemoteEcho(text);
            if (!clipboard::WriteTextWithRetry(*clipboard_platform_, text)) {
                LOGE("Failed to apply remote clipboard text after retries");
                return;
            }
            runtime_bridge_->SendRemoteClipboardResponse(text);
            return;
        }
        if (info.type() != kClipboardFiles) {
            return;
        }
        std::vector<ClipboardFile> target_files{};
        std::unordered_set<std::string> names{};
        for (const auto& file : info.files()) {
            if (!IsClipboardFileDescriptorValid(file.file_name(), file.full_path(), file.total_size()) || !names.insert(file.full_path()).second) {
                return;
            }
            target_files.push_back(file);
        }
        if (target_files.empty()) {
            return;
        }
        const auto virtual_file = CreateVirtualFile(runtime_bridge_);
        if (!virtual_file) {
            return;
        }
        // Publish only a fully initialized offer; callbacks may run inside OleSetClipboard.
        virtual_file->OnClipboardFilesInfo(target_files);
        {
            std::lock_guard lock(virtual_file_mutex_);
            virtual_file_ = virtual_file;
        }
        bool cleared{};
        for (int attempt{}; attempt < 20; ++attempt) {
            if (!update_epoch_.IsCurrent(epoch) || !runtime_bridge_->IsEnabled()) {
                CancelVirtualFiles();
                return;
            }
            if (clipboard_platform_->Clear()) {
                cleared = true;
                break;
            }
            TimeUtil::DelayBySleep(10);
        }
        if (cleared) {
            for (int attempt{}; attempt < 20; ++attempt) {
                if (!update_epoch_.IsCurrent(epoch) || !runtime_bridge_->IsEnabled()) {
                    break;
                }
                if (::OleSetClipboard(virtual_file.Get()) == S_OK) {
                    return;
                }
                TimeUtil::DelayBySleep(10);
            }
        }
        CancelVirtualFiles();
        LOGE("Failed to publish remote clipboard file offer");
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
        if (!msg || !msg->has_cp_resp_buffer()) {
            return;
        }
        Microsoft::WRL::ComPtr<CpVirtualFile> virtual_file{};
        {
            std::lock_guard lock(virtual_file_mutex_);
            virtual_file = virtual_file_;
        }
        if (virtual_file) {
            virtual_file->OnClipboardRespBuffer(msg->cp_resp_buffer());
        }
    }
}
