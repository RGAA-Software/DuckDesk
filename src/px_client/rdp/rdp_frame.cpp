#include "rdp_frame.h"

#include <algorithm>

namespace px::rdp {

bool DesktopFrame::IsValid() const noexcept {
    if (desktop.width() <= 0 || desktop.height() <= 0 || desktop.width() > 8192 || desktop.height() > 8192 ||
        static_cast<qsizetype>(desktop.width()) * desktop.height() * 4 > kMaximumFrameBytes || rectangles.empty() || rectangles.size() > 4096 ||
        pixels.size() > kMaximumFrameBytes) {
        return false;
    }
    qsizetype expected{};
    for (const auto& rectangle : rectangles) {
        if (rectangle.x() < 0 || rectangle.y() < 0 || rectangle.width() <= 0 || rectangle.height() <= 0 || rectangle.width() > desktop.width() ||
            rectangle.height() > desktop.height() || rectangle.x() > desktop.width() - rectangle.width() ||
            rectangle.y() > desktop.height() - rectangle.height()) {
            return false;
        }
        const auto count = static_cast<qsizetype>(rectangle.width()) * rectangle.height() * 4;
        if (count > kMaximumFrameBytes - expected) {
            return false;
        }
        expected += count;
    }
    return expected == pixels.size();
}

QRect DesktopViewport(QSize desktop, QSize view) noexcept {
    if (desktop.isEmpty() || view.isEmpty()) {
        return {};
    }
    const auto scaled = desktop.scaled(view, Qt::KeepAspectRatio);
    return {(view.width() - scaled.width()) / 2, (view.height() - scaled.height()) / 2, scaled.width(), scaled.height()};
}

std::optional<QPoint> MapDesktopPoint(QSize desktop, QSize view, QPoint point, bool clamp) noexcept {
    const auto target = DesktopViewport(desktop, view);
    if (target.isEmpty() || (!clamp && !target.contains(point))) {
        return {};
    }
    // The demo divided by the unscaled desktop size; use the actual fitted viewport.
    const auto x = (static_cast<std::int64_t>(point.x()) - target.x()) * desktop.width() / target.width();
    const auto y = (static_cast<std::int64_t>(point.y()) - target.y()) * desktop.height() / target.height();
    return QPoint{static_cast<int>(std::clamp<std::int64_t>(x, 0, desktop.width() - 1)),
                  static_cast<int>(std::clamp<std::int64_t>(y, 0, desktop.height() - 1))};
}

} // namespace px::rdp
