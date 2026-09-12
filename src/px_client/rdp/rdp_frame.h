#pragma once

#include <cstddef>
#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

namespace px::rdp {

inline constexpr std::size_t kMaximumFrameBytes{64U * 1024U * 1024U};

struct Size final {
    int width{};
    int height{};
    [[nodiscard]] bool Empty() const noexcept { return width <= 0 || height <= 0; }
    auto operator<=>(const Size&) const = default;
};

struct Point final {
    int x{};
    int y{};
};

struct Rectangle final {
    int x{};
    int y{};
    int width{};
    int height{};
    [[nodiscard]] bool Empty() const noexcept { return width <= 0 || height <= 0; }
};

struct DesktopFrame final {
    std::uint64_t frameId{};
    std::int64_t capturedMicroseconds{};
    Size desktop{};
    std::vector<Rectangle> rectangles{};
    std::vector<std::uint8_t> pixels{};
    [[nodiscard]] bool IsValid() const noexcept;
};

[[nodiscard]] Rectangle DesktopViewport(Size desktop, Size view) noexcept;
[[nodiscard]] std::optional<Point> MapDesktopPoint(Size desktop, Size view, Point point, bool clamp = false) noexcept;

} // namespace px::rdp
