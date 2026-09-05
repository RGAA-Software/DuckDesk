#ifndef PX_COMMON_NEW_FILE_TRANSFER_SEND_RESULT_H
#define PX_COMMON_NEW_FILE_TRANSFER_SEND_RESULT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace px {

inline constexpr std::int64_t kMaxFileTransferQueuedMessages = 256;
inline constexpr std::int64_t kFileTransferQueueLowWatermark =
    kMaxFileTransferQueuedMessages / 4;

enum class FileTransferWritableOutcome {
    kPending,
    kWritable,
    kClosed,
};

// One-shot, thread-safe bridge between a transport completion callback and the
// FT session coroutine. It deliberately exposes no asio type across DLL
// boundaries; the consumer resumes on its own executor.
class FileTransferWritableSignal final {
public:
    using Callback = std::function<void(FileTransferWritableOutcome)>;

    static std::shared_ptr<FileTransferWritableSignal> Create() {
        return std::make_shared<FileTransferWritableSignal>();
    }

    FileTransferWritableSignal() = default;

    void Subscribe(Callback callback) {
        if (!callback) {
            return;
        }
        FileTransferWritableOutcome outcome = FileTransferWritableOutcome::kPending;
        {
            std::lock_guard lock(mutex_);
            outcome = outcome_;
            if (outcome == FileTransferWritableOutcome::kPending) {
                callbacks_.push_back(std::move(callback));
                return;
            }
        }
        Invoke(callback, outcome);
    }

    void NotifyWritable() {
        Complete(FileTransferWritableOutcome::kWritable);
    }

    void Close() {
        Complete(FileTransferWritableOutcome::kClosed);
    }

    [[nodiscard]] FileTransferWritableOutcome outcome() const {
        std::lock_guard lock(mutex_);
        return outcome_;
    }

private:
    static void Invoke(const Callback& callback,
                       FileTransferWritableOutcome outcome) noexcept {
        try {
            callback(outcome);
        } catch (...) {
            // Transport completion threads must never inherit subscriber errors.
        }
    }

    void Complete(FileTransferWritableOutcome outcome) {
        std::vector<Callback> callbacks;
        {
            std::lock_guard lock(mutex_);
            if (outcome_ != FileTransferWritableOutcome::kPending) {
                return;
            }
            outcome_ = outcome;
            callbacks.swap(callbacks_);
        }
        for (const auto& callback : callbacks) {
            Invoke(callback, outcome);
        }
    }

    mutable std::mutex mutex_;
    FileTransferWritableOutcome outcome_ = FileTransferWritableOutcome::kPending;
    std::vector<Callback> callbacks_;
};

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

    static FileTransferSendResult Busy(
        std::string detail = {},
        std::shared_ptr<FileTransferWritableSignal> writable_signal = {}) {
        return FileTransferSendResult(FileTransferSendStatus::kBusy,
                                      std::move(detail),
                                      std::move(writable_signal));
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
    [[nodiscard]] const std::shared_ptr<FileTransferWritableSignal>&
    writable_signal() const noexcept {
        return writable_signal_;
    }

private:
    FileTransferSendResult(
        FileTransferSendStatus status,
        std::string detail,
        std::shared_ptr<FileTransferWritableSignal> writable_signal = {})
        : status_(status),
          detail_(std::move(detail)),
          writable_signal_(std::move(writable_signal)) {}

    FileTransferSendStatus status_ = FileTransferSendStatus::kTransportError;
    std::string detail_;
    std::shared_ptr<FileTransferWritableSignal> writable_signal_;
};

} // namespace px

#endif // PX_COMMON_NEW_FILE_TRANSFER_SEND_RESULT_H
