#include "network/file_transfer_report_delivery.h"

#include <limits>
#include <utility>

namespace px {

void FileTransferReportDelivery::RecordProgress(const std::uint64_t transferred_bytes) {
    if (!terminal_requested_) {
        latest_transferred_bytes_ = transferred_bytes;
    }
}

void FileTransferReportDelivery::RecordTerminal(const std::uint64_t transferred_bytes, const int outcome,
                                                std::optional<std::array<std::uint8_t, 32>> verified_sha256) {
    if (terminal_requested_) {
        return;
    }
    latest_transferred_bytes_ = transferred_bytes;
    terminal_outcome_ = outcome;
    terminal_sha256_ = std::move(verified_sha256);
    terminal_requested_ = true;
}

bool FileTransferReportDelivery::TerminalRequested() const { return terminal_requested_; }

bool FileTransferReportDelivery::HasPendingSnapshot() const { return pending_snapshot_.has_value(); }

std::optional<FileTransferReportSnapshot> FileTransferReportDelivery::PrepareSnapshot(const int progress_outcome) {
    if (pending_snapshot_) {
        return pending_snapshot_;
    }
    if (accepted_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    pending_snapshot_ = FileTransferReportSnapshot{
        .sequence = accepted_sequence_ + 1,
        .transferred_bytes = latest_transferred_bytes_,
        .outcome = terminal_requested_ ? terminal_outcome_ : progress_outcome,
        .verified_sha256 = terminal_requested_ ? terminal_sha256_ : std::nullopt,
        .terminal = terminal_requested_,
    };
    return pending_snapshot_;
}

bool FileTransferReportDelivery::Accept(const std::uint64_t sequence) {
    if (!pending_snapshot_ || pending_snapshot_->sequence != sequence) {
        return false;
    }
    accepted_sequence_ = sequence;
    pending_snapshot_.reset();
    return true;
}

}  // namespace px
