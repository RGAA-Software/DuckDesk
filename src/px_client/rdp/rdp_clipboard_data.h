#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QUrl>
#include <memory>
#include <optional>
#include <span>

namespace px::rdp {

inline constexpr qsizetype kClipboardDataLimit{16 * 1024 * 1024};
inline constexpr qsizetype kClipboardImageLimit{64 * 1024 * 1024};
inline constexpr std::uint64_t kClipboardFilesLimit{256 * 1024 * 1024};
inline constexpr std::size_t kClipboardEntryLimit{1024};

class ClipboardFiles;
struct ClipboardData final {
    std::optional<QString> text{};
    std::optional<QString> html{};
    QImage image{};
    QList<QUrl> urls{};
    // Owns read handles for local offers or the private staging tree for received
    // files. Qt's clipboard MIME object keeps received files alive until replaced.
    std::shared_ptr<ClipboardFiles> files{};
};

[[nodiscard]] QByteArray EncodeClipboardText(const QString& text);
[[nodiscard]] std::optional<QString> DecodeClipboardText(std::span<const unsigned char> bytes);
[[nodiscard]] QByteArray EncodeClipboardHtml(const QString& html);
[[nodiscard]] std::optional<QString> DecodeClipboardHtml(std::span<const unsigned char> bytes);
[[nodiscard]] QByteArray EncodeClipboardDib(const QImage& image);
[[nodiscard]] QImage DecodeClipboardDib(std::span<const unsigned char> bytes);
[[nodiscard]] bool SafeClipboardRelativePath(const QString& path);

} // namespace px::rdp
