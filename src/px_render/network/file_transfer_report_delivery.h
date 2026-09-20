#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace px {

struct FileTransferReportSnapshot final {
    std::uint64_t sequence{};
    std::uint64_t transferred_bytes{};
    int outcome{};
    std::optional<std::array<std::uint8_t, 32>> verified_sha256{};
    bool terminal{};
};

class FileTransferReportDelivery final {
public:
    void RecordProgress(std::uint64_t transferred_bytes);
    void RecordTerminal(std::uint64_t transferred_bytes, int outcome, std::optional<std::array<std::uint8_t, 32>> verified_sha256);

    [[nodiscard]] bool TerminalRequested() const;
    [[nodiscard]] bool HasPendingSnapshot() const;
    [[nodiscard]] std::optional<FileTransferReportSnapshot> PrepareSnapshot(int progress_outcome);
    [[nodiscard]] bool Accept(std::uint64_t sequence);

private:
    std::uint64_t accepted_sequence_{};
    std::uint64_t latest_transferred_bytes_{};
    int terminal_outcome_{};
    std::optional<std::array<std::uint8_t, 32>> terminal_sha256_{};
    std::optional<FileTransferReportSnapshot> pending_snapshot_{};
    bool terminal_requested_{};
};

}  // namespace px
