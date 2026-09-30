#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace px::client::imgui {

// Ratios describe the ball's travel within the video area, excluding the title bar and its own diameter.
struct ControllerPosition final {
    float horizontalRatio{};
    float verticalRatio{};

    [[nodiscard]] bool IsValid() const noexcept {
        return std::isfinite(horizontalRatio) && std::isfinite(verticalRatio) && horizontalRatio >= 0.0F && horizontalRatio <= 1.0F &&
               verticalRatio >= 0.0F && verticalRatio <= 1.0F;
    }
};

struct ControllerPoint final {
    float x{};
    float y{};
};

struct ControllerArea final {
    float left{};
    float top{};
    float width{};
    float height{};
    float diameter{};

    [[nodiscard]] float HorizontalTravel() const noexcept { return std::max(0.0F, width - diameter); }
    [[nodiscard]] float VerticalTravel() const noexcept { return std::max(0.0F, height - diameter); }

    [[nodiscard]] ControllerPoint Clamp(const ControllerPoint position) const noexcept {
        return {std::clamp(position.x, left, left + HorizontalTravel()), std::clamp(position.y, top, top + VerticalTravel())};
    }

    [[nodiscard]] ControllerPosition Normalize(const ControllerPoint position) const noexcept {
        const ControllerPoint bounded{Clamp(position)};
        return {HorizontalTravel() > 0.0F ? (bounded.x - left) / HorizontalTravel() : 0.0F,
                VerticalTravel() > 0.0F ? (bounded.y - top) / VerticalTravel() : 0.0F};
    }

    [[nodiscard]] ControllerPoint Restore(const std::optional<ControllerPosition>& position, const float initialMargin) const noexcept {
        if (!position || !position->IsValid()) return Clamp({left + initialMargin, top + initialMargin});
        return {left + HorizontalTravel() * position->horizontalRatio, top + VerticalTravel() * position->verticalRatio};
    }
};

}  // namespace px::client::imgui
