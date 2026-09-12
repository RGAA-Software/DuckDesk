#include "rdp_frame.h"

#include <algorithm>

namespace px::rdp {

bool DesktopFrame::IsValid() const noexcept {
    if (desktop.Empty() || desktop.width > 8192 || desktop.height > 8192 ||
        static_cast<std::size_t>(desktop.width) * static_cast<std::size_t>(desktop.height) * 4U > kMaximumFrameBytes || rectangles.empty() ||
        rectangles.size() > 4096U || pixels.size() > kMaximumFrameBytes) {
        return false;
    }
    std::size_t expected{};
    for (const auto& rectangle : rectangles) {
        if (rectangle.x < 0 || rectangle.y < 0 || rectangle.Empty() || rectangle.width > desktop.width || rectangle.height > desktop.height ||
            rectangle.x > desktop.width - rectangle.width || rectangle.y > desktop.height - rectangle.height) {
            return false;
        }
        const auto count = static_cast<std::size_t>(rectangle.width) * static_cast<std::size_t>(rectangle.height) * 4U;
        if (count > kMaximumFrameBytes - expected) return false;
        expected += count;
    }
    return expected == pixels.size();
}

Rectangle DesktopViewport(const Size desktop, const Size view) noexcept {
    if (desktop.Empty() || view.Empty()) return {};
    const double scale = std::min(static_cast<double>(view.width) / desktop.width, static_cast<double>(view.height) / desktop.height);
    const int width = std::max(1, static_cast<int>(desktop.width * scale));
    const int height = std::max(1, static_cast<int>(desktop.height * scale));
    return {(view.width - width) / 2, (view.height - height) / 2, width, height};
}

std::optional<Point> MapDesktopPoint(const Size desktop, const Size view, const Point point, const bool clamp) noexcept {
    const auto target = DesktopViewport(desktop, view);
    if (target.Empty() || (!clamp && (point.x < target.x || point.y < target.y || point.x >= target.x + target.width ||
                                      point.y >= target.y + target.height))) {
        return std::nullopt;
    }
    const auto x = (static_cast<std::int64_t>(point.x) - target.x) * desktop.width / target.width;
    const auto y = (static_cast<std::int64_t>(point.y) - target.y) * desktop.height / target.height;
    return Point{static_cast<int>(std::clamp<std::int64_t>(x, 0, desktop.width - 1)),
                 static_cast<int>(std::clamp<std::int64_t>(y, 0, desktop.height - 1))};
}

} // namespace px::rdp
