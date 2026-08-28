#ifndef PX_COMMON_NEW_FILE_TRANSFER_SEND_RESULT_H
#define PX_COMMON_NEW_FILE_TRANSFER_SEND_RESULT_H

#include <string>
#include <utility>

namespace px {

enum class FileTransferSendStatus {
    kAccepted,
    kBusy,
    kDisconnected,
    kTransportError,
};

class FileTransferSendResult {
public:
    static FileTransferSendResult Accepted() {
        return FileTransferSendResult(FileTransferSendStatus::kAccepted, {});
    }

    static FileTransferSendResult Busy(std::string detail = {}) {
        return FileTransferSendResult(FileTransferSendStatus::kBusy, std::move(detail));
    }

    static FileTransferSendResult Disconnected(std::string detail = {}) {
        return FileTransferSendResult(FileTransferSendStatus::kDisconnected, std::move(detail));
    }

    static FileTransferSendResult TransportError(std::string detail = {}) {
        return FileTransferSendResult(FileTransferSendStatus::kTransportError, std::move(detail));
    }

    [[nodiscard]] bool accepted() const noexcept {
        return status_ == FileTransferSendStatus::kAccepted;
    }

    [[nodiscard]] FileTransferSendStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }

private:
    FileTransferSendResult(FileTransferSendStatus status, std::string detail)
        : status_(status), detail_(std::move(detail)) {}

    FileTransferSendStatus status_ = FileTransferSendStatus::kTransportError;
    std::string detail_;
};

} // namespace px

#endif // PX_COMMON_NEW_FILE_TRANSFER_SEND_RESULT_H
