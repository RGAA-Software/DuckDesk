#pragma once

#include <QByteArray>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <cstdint>
#include <optional>

namespace px::rdp {

inline constexpr qsizetype kMaximumFrameBytes{64 * 1024 * 1024};

// Packed BGRX dirty rectangles, as in the original Qt demo. One outstanding
// frame is acknowledged after presentation; incremental patches cannot be dropped.
struct DesktopFrame final {
    std::uint64_t frame_id{0};
    std::int64_t captured_us{0};
    QSize desktop{};
    QVector<QRect> rectangles{};
    QByteArray pixels{};
    [[nodiscard]] bool IsValid() const noexcept;
};

[[nodiscard]] QRect DesktopViewport(QSize desktop, QSize view) noexcept;
[[nodiscard]] std::optional<QPoint> MapDesktopPoint(QSize desktop, QSize view, QPoint point, bool clamp = false) noexcept;

} // namespace px::rdp
