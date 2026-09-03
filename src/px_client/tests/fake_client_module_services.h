#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "px_client/modules/client_module_services.h"
#include "px_client/modules/client_module_settings.h"

namespace px::test {

class FakeClientModuleServices final : public ClientModuleServices {
public:
    void SendClipboardUpdate(
        ClipboardType,
        std::string,
        std::vector<ClipboardFile>) override {
        ++clipboard_updates_;
    }

    void SendRemoteClipboardResponse(std::string) override {
        ++clipboard_responses_;
    }

    void PostMediaMessage(std::shared_ptr<Data>) override {
        ++media_messages_;
    }

    FileTransferSendResult PostFileTransferMessage(
        std::shared_ptr<Data>) override {
        ++file_messages_;
        return FileTransferSendResult::Accepted();
    }

    void ReportFileTransferBegin(
        std::string,
        std::string,
        std::string) override {
        ++transfer_begins_;
    }

    void ReportFileTransferEnd(
        std::string,
        bool,
        std::string,
        std::string) override {
        ++transfer_ends_;
    }

    void NotifyRecordingComplete(std::string) override {
        ++recording_notifications_;
    }

    std::atomic_int clipboard_updates_ = 0;
    std::atomic_int clipboard_responses_ = 0;
    std::atomic_int media_messages_ = 0;
    std::atomic_int file_messages_ = 0;
    std::atomic_int transfer_begins_ = 0;
    std::atomic_int transfer_ends_ = 0;
    std::atomic_int recording_notifications_ = 0;
};

inline ClientModuleConfig MakeModuleConfig(std::string name) {
    return ClientModuleConfig{
        .screen_recording_path_ = {},
        .settings_ = {
            .clipboard_enabled_ = false,
            .device_id_ = std::move(name),
            .stream_id_ = "module-test-stream",
            .language_ = 0,
            .stream_name_ = "module-test",
            .display_name_ = "module-test",
            .display_remote_name_ = "module-test-remote",
        },
    };
}

}  // namespace px::test
