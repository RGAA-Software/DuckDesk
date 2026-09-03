#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "px_common_new/file_transfer_send_result.h"
#include "px_message.pb.h"

namespace px {

class Data;

class ClientModuleServices {
public:
    virtual ~ClientModuleServices() = default;

    virtual void SendClipboardUpdate(
        ClipboardType type,
        std::string text,
        std::vector<ClipboardFile> files) = 0;
    virtual void SendRemoteClipboardResponse(std::string remote_text) = 0;
    virtual void PostMediaMessage(std::shared_ptr<Data> data) = 0;
    [[nodiscard]] virtual FileTransferSendResult PostFileTransferMessage(
        std::shared_ptr<Data> data) = 0;
    virtual void ReportFileTransferBegin(
        std::string task_id,
        std::string file_path,
        std::string direction) = 0;
    virtual void ReportFileTransferEnd(
        std::string task_id,
        bool success,
        std::string status,
        std::string reason) = 0;
    virtual void NotifyRecordingComplete(std::string directory) = 0;
};

}  // namespace px
