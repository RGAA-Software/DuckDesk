#include "px_ui/theme_tokens.h"

#include <algorithm>

namespace px::ui {
namespace {

ImVec4 Rgba(const int red, const int green, const int blue, const float alpha = 1.0F) noexcept {
    constexpr float divisor{255.0F};
    return {static_cast<float>(red) / divisor, static_cast<float>(green) / divisor, static_cast<float>(blue) / divisor, alpha};
}

}  // namespace

ThemeTokens ThemeTokensFor(const Theme theme) noexcept {
    if (theme == Theme::Light) {
        return ThemeTokens{.background = Rgba(250, 250, 250),
                           .foreground = Rgba(24, 24, 27),
                           .card = Rgba(255, 255, 255),
                           .popover = Rgba(255, 255, 255),
                           .primary = Rgba(0, 127, 73),
                           .primaryForeground = Rgba(255, 255, 255),
                           .primaryText = Rgba(0, 107, 61),
                           .secondary = Rgba(244, 244, 245),
                           .secondaryForeground = Rgba(39, 39, 42),
                           .muted = Rgba(244, 244, 245),
                           .mutedForeground = Rgba(113, 113, 122),
                           .accent = Rgba(236, 253, 245),
                           .accentForeground = Rgba(0, 107, 61),
                           .destructive = Rgba(220, 38, 38),
                           .destructiveForeground = Rgba(255, 255, 255),
                           .destructiveText = Rgba(190, 24, 38),
                           .border = Rgba(228, 228, 231),
                           .input = Rgba(212, 212, 216),
                           .ring = Rgba(0, 154, 89),
                           .success = Rgba(22, 163, 74),
                           .warning = Rgba(217, 119, 6)};
    }
    return ThemeTokens{.background = Rgba(34, 49, 67),
                       .foreground = Rgba(242, 247, 251),
                       .card = Rgba(45, 64, 84),
                       .popover = Rgba(53, 75, 96),
                       .primary = Rgba(8, 127, 104),
                       .primaryForeground = Rgba(255, 255, 255),
                       .primaryText = Rgba(99, 215, 186),
                       .secondary = Rgba(58, 79, 101),
                       .secondaryForeground = Rgba(235, 244, 249),
                       .muted = Rgba(70, 91, 113),
                       .mutedForeground = Rgba(201, 214, 226),
                       .accent = Rgba(29, 98, 89),
                       .accentForeground = Rgba(216, 251, 241),
                       .destructive = Rgba(190, 45, 63),
                       .destructiveForeground = Rgba(255, 255, 255),
                       .destructiveText = Rgba(255, 158, 153),
                       .border = Rgba(88, 111, 135),
                       .input = Rgba(105, 129, 151),
                       .ring = Rgba(83, 212, 179),
                       .success = Rgba(78, 208, 153),
                       .warning = Rgba(246, 194, 103)};
}

ThemeTokens CurrentThemeTokens() noexcept {
    const ImVec4 background{ImGui::GetStyle().Colors[ImGuiCol_WindowBg]};
    const float luminance{background.x * 0.2126F + background.y * 0.7152F + background.z * 0.0722F};
    return ThemeTokensFor(luminance > 0.5F ? Theme::Light : Theme::Dark);
}

UiMetrics MetricsFor(const float scale) noexcept {
    const float safeScale{std::max(0.5F, scale)};
    return UiMetrics{.scale = safeScale,
                     .controlXs = 24.0F * safeScale,
                     .controlSm = 24.0F * safeScale,
                     .controlDefault = 32.0F * safeScale,
                     .controlLg = 40.0F * safeScale,
                     .controlRadius = 6.0F * safeScale,
                     .cardRadius = 8.0F * safeScale,
                     .popupRadius = 8.0F * safeScale,
                     .fontSize = 14.0F * safeScale,
                     .checkboxSize = 16.0F * safeScale,
                     .switchWidth = 44.0F * safeScale,
                     .switchHeight = 22.0F * safeScale,
                     .tableRowHeight = 32.0F * safeScale,
                     .borderWidth = std::max(1.0F, safeScale),
                     .spacingXs = 4.0F * safeScale,
                     .spacingSm = 8.0F * safeScale,
                     .spacingMd = 12.0F * safeScale,
                     .spacingLg = 16.0F * safeScale,
                     .spacingXl = 24.0F * safeScale,
                     .iconSm = 14.0F * safeScale,
                     .iconDefault = 16.0F * safeScale,
                     .iconLg = 20.0F * safeScale};
}

}  // namespace px::ui
