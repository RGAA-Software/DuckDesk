#pragma once

#include "rdp_clipboard_data.h"
#include "px_common/win32/unique_win_handle.h"
#include <QTemporaryDir>
#include <vector>

namespace px::rdp {

struct ClipboardFileEntry final {
    QString name{};
    std::uint64_t size{0};
    bool directory{false};
};

class ClipboardFiles final {
  public:
    [[nodiscard]] static std::shared_ptr<ClipboardFiles> Offer(const QList<QUrl>& urls);
    [[nodiscard]] static std::shared_ptr<ClipboardFiles> Receive(std::span<const unsigned char> descriptors);
    [[nodiscard]] QByteArray Descriptors() const;
    [[nodiscard]] const std::vector<ClipboardFileEntry>& Entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] QByteArray Read(std::size_t index, std::uint64_t offset, std::uint32_t length) const;
    [[nodiscard]] bool Write(std::size_t index, std::uint64_t offset, std::span<const unsigned char> bytes);
    [[nodiscard]] QList<QUrl> Publish();
    ClipboardFiles() = default;

  private:
    [[nodiscard]] bool AddLocal(const QString& absolute, const QString& relative);
    [[nodiscard]] bool HoldParents(const QString& absolute);
    // Destruction closes every handle before the owned temporary tree is removed.
    std::unique_ptr<QTemporaryDir> staging_{};
    std::vector<ClipboardFileEntry> entries_{};
    std::vector<UniqueWinHandle> handles_{};
    std::vector<UniqueWinHandle> directory_handles_{};
    QStringList held_directories_{};
    QStringList roots_{};
    std::uint64_t total_size_{0};
    std::vector<std::uint64_t> received_{};
    bool published_{false};
};

} // namespace px::rdp
