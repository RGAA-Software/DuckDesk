#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "px_client/modules/client_module_settings.h"
#include "px_message.pb.h"

namespace px {

class ClipboardFileWrapper;
class ClientModuleServices;
class Data;
class Message;

class ClipboardRuntimeBridge final {
public:
    explicit ClipboardRuntimeBridge(
        std::weak_ptr<ClientModuleServices> services);

    void Activate(const ClientModuleSettings& settings);
    void UpdateSettings(const ClientModuleSettings& settings);
    void Deactivate();
    [[nodiscard]] bool IsEnabled() const;
    [[nodiscard]] ClientModuleSettings SettingsSnapshot() const;
    [[nodiscard]] const std::shared_ptr<std::atomic_bool>& LifetimeToken() const;

    void SendClipboardUpdate(
        ClipboardType type,
        std::string text,
        std::vector<ClipboardFile> files) const;
    void SendRemoteClipboardResponse(std::string remote_text) const;
    void PostMediaMessage(std::shared_ptr<Data> data) const;
    void ReportFileTransferBegin(
        std::string task_id,
        std::string file_path,
        std::string direction) const;
    void ReportFileTransferEnd(
        std::string task_id,
        bool success,
        std::string status,
        std::string reason) const;
    [[nodiscard]] bool RequestBuffer(
        const ClipboardFileWrapper& file_wrapper,
        std::int64_t request_index,
        std::int64_t request_start,
        unsigned long request_size) const;

    void OnRequestFileBegin(const std::shared_ptr<Message>& message) const;
    void OnRequestFileBuffer(const std::shared_ptr<Message>& message) const;
    void OnRequestFileEnd(const std::shared_ptr<Message>& message) const;

private:
    mutable std::mutex mutex_;
    ClientModuleSettings settings_;
    std::weak_ptr<ClientModuleServices> services_;
    std::shared_ptr<std::atomic_bool> lifetime_token_ =
        std::make_shared<std::atomic_bool>(false);
};

}  // namespace px
