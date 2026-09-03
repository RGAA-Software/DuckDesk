#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "px_client/modules/client_module_settings.h"

namespace px {

class ClientModuleContext;
class ClientModuleServices;
class MediaRecordRuntime;
class Message;

class ClientMediaRecordingModule final {
public:
    explicit ClientMediaRecordingModule(
        std::weak_ptr<ClientModuleServices> services);
    ~ClientMediaRecordingModule();

    ClientMediaRecordingModule(const ClientMediaRecordingModule&) = delete;
    ClientMediaRecordingModule& operator=(
        const ClientMediaRecordingModule&) = delete;

    bool Start(const ClientModuleConfig& config);
    void Stop();
    void HandleMessage(const std::shared_ptr<Message>& message);
    void UpdateSettings(const ClientModuleSettings& settings);

    void StartRecording();
    void StopRecording();
    [[nodiscard]] std::string GetScreenRecordingPath() const;

private:
    std::weak_ptr<ClientModuleServices> services_;
    std::shared_ptr<ClientModuleContext> context_;
    std::shared_ptr<MediaRecordRuntime> runtime_;
    std::string screen_recording_path_;
    mutable std::mutex lifecycle_mutex_;
    bool stopped_ = true;
};

}  // namespace px
