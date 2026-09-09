#include "rdp_clipboard_data.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace px::rdp {
namespace {
constexpr qsizetype kDibHeader{40};
std::optional<QString> Utf8(std::span<const unsigned char> bytes) {
    if (bytes.size() > static_cast<std::size_t>(kClipboardDataLimit)) {
        return {};
    }
    auto decoder = QStringDecoder{QStringDecoder::Utf8, QStringConverter::Flag::Stateless};
    QString result = decoder(QByteArrayView{reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())});
    return decoder.hasError() ? std::nullopt : std::optional<QString>{std::move(result)};
}
} // namespace

QByteArray EncodeClipboardText(const QString& text) {
    if (text.size() > (kClipboardDataLimit / 2) - 1 || text.contains(QChar{0})) {
        return {};
    }
    QByteArray result((text.size() + 1) * 2, '\0');
    for (qsizetype index{}; index < text.size(); ++index) {
        qToLittleEndian<quint16>(text.at(index).unicode(), result.data() + index * 2);
    }
    return result;
}

std::optional<QString> DecodeClipboardText(std::span<const unsigned char> bytes) {
    if (bytes.size() < 2 || bytes.size() > kClipboardDataLimit || bytes.size() % 2 != 0 ||
        qFromLittleEndian<quint16>(bytes.data() + bytes.size() - 2) != 0) {
        return {};
    }
    QString result{};
    result.reserve(static_cast<qsizetype>(bytes.size() / 2 - 1));
    bool high_surrogate{false};
    for (std::size_t offset{}; offset + 2 < bytes.size(); offset += 2) {
        const QChar character{qFromLittleEndian<quint16>(bytes.data() + offset)};
        if (character.isNull() || (high_surrogate && !character.isLowSurrogate()) || (!high_surrogate && character.isLowSurrogate())) {
            return {};
        }
        high_surrogate = character.isHighSurrogate();
        result.append(character);
    }
    return high_surrogate ? std::nullopt : std::optional<QString>{std::move(result)};
}

QByteArray EncodeClipboardHtml(const QString& html) {
    if (html.size() > kClipboardDataLimit || html.contains(QChar{0})) {
        return {};
    }
    const auto fragment = html.toUtf8();
    if (fragment.size() > kClipboardDataLimit - 512) {
        return {};
    }
    const QByteArray prefix{"<html><body><!--StartFragment-->"};
    const QByteArray suffix{"<!--EndFragment--></body></html>"};
    QByteArray header{"Version:1.0\r\nStartHTML:0000000000\r\nEndHTML:0000000000\r\nStartFragment:0000000000\r\nEndFragment:0000000000\r\n"};
    const auto start = header.size();
    const std::array<std::pair<QByteArray, qsizetype>, 4> offsets{{{"StartHTML:", start},
                                                                   {"EndHTML:", start + prefix.size() + fragment.size() + suffix.size()},
                                                                   {"StartFragment:", start + prefix.size()},
                                                                   {"EndFragment:", start + prefix.size() + fragment.size()}}};
    for (const auto& [key, value] : offsets) {
        header.replace(header.indexOf(key) + key.size(), 10, QByteArray::number(value).rightJustified(10, '0'));
    }
    return header + prefix + fragment + suffix + '\0';
}

std::optional<QString> DecodeClipboardHtml(std::span<const unsigned char> bytes) {
    if (bytes.empty() || bytes.size() > kClipboardDataLimit) {
        return {};
    }
    const QByteArrayView data{reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())};
    const auto header = QByteArray{data.first(std::min<qsizetype>(data.size(), 4096))};
    const auto offset = [&header](QByteArray key) -> std::optional<qsizetype> {
        const auto position = header.indexOf(key);
        if (position < 0 || (position > 0 && header.at(position - 1) != '\n') || header.indexOf(key, position + 1) >= 0) {
            return {};
        }
        const auto begin = position + key.size();
        const auto end = header.indexOf('\n', begin);
        if (end < begin || end - begin > 24) {
            return {};
        }
        const auto number = header.mid(begin, end - begin).trimmed();
        if (number.isEmpty() || !std::ranges::all_of(number, [](char byte) { return byte >= '0' && byte <= '9'; })) {
            return {};
        }
        bool ok{false};
        const auto value = number.toLongLong(&ok);
        return ok && value >= 0 && value <= kClipboardDataLimit ? std::optional<qsizetype>{value} : std::nullopt;
    };
    const auto html_begin = offset("StartHTML:");
    const auto html_end = offset("EndHTML:");
    const auto begin = offset("StartFragment:");
    const auto end = offset("EndFragment:");
    if (!html_begin || !html_end || !begin || !end || *html_begin > *begin || *begin > *end || *end > *html_end || *html_end > data.size()) {
        return {};
    }
    return Utf8(bytes.subspan(static_cast<std::size_t>(*begin), static_cast<std::size_t>(*end - *begin)));
}

QByteArray EncodeClipboardDib(const QImage& image) {
    if (image.isNull() || image.width() > 8192 || image.height() > 8192 ||
        static_cast<std::int64_t>(image.width()) * image.height() * 4 > kClipboardImageLimit - kDibHeader) {
        return {};
    }
    const auto converted = image.convertToFormat(QImage::Format_RGB32);
    if (converted.isNull()) {
        return {};
    }
    const qsizetype stride = converted.width() * 4;
    QByteArray result(kDibHeader + stride * converted.height(), '\0');
    qToLittleEndian<quint32>(kDibHeader, result.data());
    qToLittleEndian<quint32>(converted.width(), result.data() + 4);
    qToLittleEndian<quint32>(converted.height(), result.data() + 8);
    qToLittleEndian<quint16>(1, result.data() + 12);
    qToLittleEndian<quint16>(32, result.data() + 14);
    qToLittleEndian<quint32>(static_cast<quint32>(converted.height() * stride), result.data() + 20);
    for (int row{}; row < converted.height(); ++row) {
        std::memcpy(result.data() + kDibHeader + row * stride, converted.constScanLine(converted.height() - row - 1), stride);
    }
    return result;
}

QImage DecodeClipboardDib(std::span<const unsigned char> bytes) {
    if (bytes.size() < kDibHeader || bytes.size() > kClipboardImageLimit) {
        return {};
    }
    const auto header = qFromLittleEndian<quint32>(bytes.data());
    const auto width = qFromLittleEndian<qint32>(bytes.data() + 4);
    const auto height = qFromLittleEndian<qint32>(bytes.data() + 8);
    const auto planes = qFromLittleEndian<quint16>(bytes.data() + 12);
    const auto bits = qFromLittleEndian<quint16>(bytes.data() + 14);
    const auto compression = qFromLittleEndian<quint32>(bytes.data() + 16);
    const auto colors = qFromLittleEndian<quint32>(bytes.data() + 32);
    if (header != kDibHeader || width <= 0 || width > 8192 || height == 0 || height < -8192 || height > 8192 || planes != 1 ||
        (bits != 24 && bits != 32) || (compression != 0 && compression != 3) || colors != 0) {
        return {};
    }
    auto pixel_offset = header;
    if (compression == 3) {
        // Windows commonly publishes 32-bit CF_DIB with three explicit RGB
        // masks. Accept only the standard BGRX layout, never arbitrary masks.
        if (bits != 32 || bytes.size() < header + 12 || qFromLittleEndian<quint32>(bytes.data() + header) != 0x00ff0000 ||
            qFromLittleEndian<quint32>(bytes.data() + header + 4) != 0x0000ff00 ||
            qFromLittleEndian<quint32>(bytes.data() + header + 8) != 0x000000ff) {
            return {};
        }
        pixel_offset += 12;
    }
    const auto rows = std::abs(height);
    const auto stride = (static_cast<qsizetype>(width) * bits + 31) / 32 * 4;
    const auto wire_size = stride * rows;
    if (wire_size > static_cast<qsizetype>(bytes.size()) - pixel_offset || static_cast<qsizetype>(width) * rows * 4 > kClipboardImageLimit) {
        return {};
    }
    QImage result{width, rows, QImage::Format_RGB32};
    if (result.isNull()) {
        return {};
    }
    for (int row{}; row < rows; ++row) {
        const auto source = bytes.subspan(pixel_offset + static_cast<std::size_t>(height > 0 ? rows - row - 1 : row) * stride, stride);
        auto target = std::span<unsigned char>{result.scanLine(row), static_cast<std::size_t>(width) * 4};
        for (int column{}; column < width; ++column) {
            const auto src = static_cast<std::size_t>(column) * (bits / 8);
            const auto dst = static_cast<std::size_t>(column) * 4;
            target[dst] = source[src];
            target[dst + 1] = source[src + 1];
            target[dst + 2] = source[src + 2];
            target[dst + 3] = 255;
        }
    }
    return result;
}

bool SafeClipboardRelativePath(const QString& path) {
    // FILEDESCRIPTORW has a 260-WCHAR relative name. Refuse ambiguous Windows
    // names, ADS, device aliases, traversal and normalization collisions.
    if (path.isEmpty() || path.size() >= 260 || path.contains('/') || path.contains(':') || path.contains(QChar{0})) {
        return false;
    }
    const auto components = path.split('\\');
    if (components.size() > 32) {
        return false;
    }
    for (const auto& component : components) {
        if (component.isEmpty() || component == "." || component == ".." || component.endsWith('.') || component.endsWith(' ') ||
            component.contains(QRegularExpression{QStringLiteral("[\x01-\x1f<>\"|?*]")})) {
            return false;
        }
        const auto stem = component.section('.', 0, 0).toUpper();
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CLOCK$" ||
            stem.contains(QRegularExpression{QStringLiteral("^(COM|LPT)[0-9¹²³]$")})) {
            return false;
        }
    }
    return true;
}
} // namespace px::rdp
