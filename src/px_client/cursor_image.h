#pragma once

#include <QImage>
#include <QPoint>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace px {
// Standard CEF cursors carry a type without pixels. Hidden state takes precedence over either representation.
inline std::optional<Qt::CursorShape> ResolveCursorShape(bool visible, bool has_bitmap, Qt::CursorShape standard_shape) {
    if (!visible) {
        return Qt::BlankCursor;
    }
    if (has_bitmap) {
        return std::nullopt;
    }
    return standard_shape == Qt::BitmapCursor ? Qt::ArrowCursor : standard_shape;
}

struct CursorImage final {
    QImage image{};
    QPoint logical_hotspot{};
};

// The wire bitmap and hotspot use physical pixels; Qt cursor hotspots use logical pixels.
inline std::optional<CursorImage> MakeCursorImage(std::span<const char> rgba, uint32_t width, uint32_t height, uint32_t hotspot_x, uint32_t hotspot_y,
                                                  qreal device_pixel_ratio) {
    constexpr auto max_dimension = std::numeric_limits<int>::max() / 4;
    if (width == 0 || height == 0 || width > max_dimension || height > max_dimension || hotspot_x >= width || hotspot_y >= height ||
        !std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0) {
        return std::nullopt;
    }
    const auto required_bytes = uint64_t{width} * height * 4;
    if (required_bytes != rgba.size()) {
        return std::nullopt;
    }
    const auto logical_x = hotspot_x / device_pixel_ratio;
    const auto logical_y = hotspot_y / device_pixel_ratio;
    if (logical_x > std::numeric_limits<int>::max() - 0.5 || logical_y > std::numeric_limits<int>::max() - 0.5) {
        return std::nullopt;
    }
    QImage image(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888);
    if (image.isNull()) {
        return std::nullopt;
    }
    // Qt/C boundary: copy immediately into QImage-owned storage; never retain the incoming buffer.
    std::memcpy(image.bits(), rgba.data(), rgba.size());
    // Without this DPR, Qt scales captured physical pixels again on every self-loop capture.
    image.setDevicePixelRatio(device_pixel_ratio);
    return CursorImage{std::move(image), QPoint(qRound(logical_x), qRound(logical_y))};
}
} // namespace px
