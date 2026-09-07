#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "client_module_services.h"
#include "client_module_settings.h"

namespace px {

class BaseWorkspace;
class ClientClipboardModule;
class ClientContext;
class ClientFileTransferModule;
class ClientMediaRecordingModule;
class Message;

class ClientModuleManager final
    : public ClientModuleServices,
      public std::enable_shared_from_this<ClientModuleManager> {
public:
    static std::shared_ptr<ClientModuleManager> Make(
        const std::shared_ptr<BaseWorkspace>& workspace);

    explicit ClientModuleManager(
        const std::shared_ptr<BaseWorkspace>& workspace);
    ~ClientModuleManager() override;

    bool Start();
    void Stop();
    void HandleMessage(const std::shared_ptr<Message>& message);
    void UpdateSettings(const ClientModuleSettings& settings);
    void OnTransportConnected();

    [[nodiscard]] std::shared_ptr<ClientClipboardModule>
    GetClipboardModule() const;
    [[nodiscard]] std::shared_ptr<ClientFileTransferModule>
    GetFileTransferModule() const;
    [[nodiscard]] std::shared_ptr<ClientMediaRecordingModule>
    GetMediaRecordingModule() const;

    void SendClipboardUpdate(
        ClipboardType type,
        std::string text,
        std::vector<ClipboardFile> files) override;
    void SendRemoteClipboardResponse(std::string remote_text) override;
    void PostMediaMessage(std::shared_ptr<Data> data) override;
    [[nodiscard]] FileTransferSendResult PostFileTransferMessage(
        std::shared_ptr<Data> data) override;
    void ReportFileTransferBegin(
        std::string task_id,
        std::string file_path,
        std::string direction) override;
    void ReportFileTransferEnd(
        std::string task_id,
        bool success,
        std::string status,
        std::string reason) override;
    void NotifyRecordingComplete(std::string directory) override;

private:
    [[nodiscard]] ClientModuleConfig BuildConfig() const;

    std::weak_ptr<BaseWorkspace> workspace_;
    std::weak_ptr<ClientContext> context_;
    mutable std::mutex modules_mutex_;
    std::shared_ptr<ClientClipboardModule> clipboard_;
    std::shared_ptr<ClientFileTransferModule> file_transfer_;
    std::shared_ptr<ClientMediaRecordingModule> media_recording_;
    std::atomic_bool stopping_ = true;
};

}  // namespace px
