#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "px_client/modules/client_module_services.h"
#include "px_client/modules/client_module_settings.h"

namespace px::test {

class FakeClientModuleServices final : public ClientModuleServices {
public:
  void SendClipboardUpdate(ClipboardType, std::string, std::vector<ClipboardFile> files) override {
      std::lock_guard lock(clipboard_mutex_);
      clipboard_files_ = std::move(files);
      ++clipboard_updates_;
  }

    void SendRemoteClipboardResponse(std::string) override {
        ++clipboard_responses_;
    }

    void PostMediaMessage(std::shared_ptr<Data>) override {
        ++media_messages_;
    }

    FileTransferSendResult PostFileTransferMessage(std::shared_ptr<Data> data) override {
        std::lock_guard lock(clipboard_mutex_);
        file_payloads_.push_back(std::move(data));
        ++file_messages_;
        return FileTransferSendResult::Accepted();
    }

    void ReportFileTransferBegin(
        std::string,
        std::string,
        std::string) override {
        ++transfer_begins_;
    }

    void ReportFileTransferEnd(std::string, bool success, std::string, std::string) override {
        std::lock_guard lock(clipboard_mutex_);
        transfer_results_.push_back(success);
        ++transfer_ends_;
    }

    void NotifyRecordingComplete(std::string) override {
        ++recording_notifications_;
    }
    void NotifyRecordingFailure(uint64_t intent, std::string) override {
        std::lock_guard lock(recording_failure_mutex_);
        recording_failure_intents_.push_back(intent);
        ++recording_failures_;
    }

    std::vector<uint64_t> RecordingFailureIntents() const {
        std::lock_guard lock(recording_failure_mutex_);
        return recording_failure_intents_;
    }

    std::vector<ClipboardFile> ClipboardFiles() const {
        std::lock_guard lock(clipboard_mutex_);
        return clipboard_files_;
    }

    std::vector<std::shared_ptr<Data>> FilePayloads() const {
        std::lock_guard lock(clipboard_mutex_);
        return file_payloads_;
    }

    std::vector<bool> TransferResults() const {
        std::lock_guard lock(clipboard_mutex_);
        return transfer_results_;
    }

    std::atomic_int clipboard_updates_ = 0;
    std::atomic_int clipboard_responses_ = 0;
    std::atomic_int media_messages_ = 0;
    std::atomic_int file_messages_ = 0;
    std::atomic_int transfer_begins_ = 0;
    std::atomic_int transfer_ends_ = 0;
    std::atomic_int recording_notifications_ = 0;
    std::atomic_int recording_failures_{0};

  private:
    mutable std::mutex clipboard_mutex_{};
    std::vector<ClipboardFile> clipboard_files_{};
    std::vector<std::shared_ptr<Data>> file_payloads_{};
    std::vector<bool> transfer_results_{};
    mutable std::mutex recording_failure_mutex_{};
    std::vector<uint64_t> recording_failure_intents_{};
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
