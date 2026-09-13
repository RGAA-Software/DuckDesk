#include "px_ui/theme_tokens.h"

#include <algorithm>

namespace px::ui {
namespace {

ImVec4 Rgba(const int red, const int green, const int blue, const float alpha = 1.0F) noexcept {
    constexpr float divisor{255.0F};
    return {static_cast<float>(red) / divisor, static_cast<float>(green) / divisor, static_cast<float>(blue) / divisor, alpha};
}

} // namespace

ThemeTokens ThemeTokensFor(const Theme theme) noexcept {
    if (theme == Theme::Light) {
        return ThemeTokens{.background = Rgba(250, 250, 250),
                           .foreground = Rgba(24, 24, 27),
                           .card = Rgba(255, 255, 255),
                           .popover = Rgba(255, 255, 255),
                           .primary = Rgba(0, 127, 73),
                           .primaryForeground = Rgba(255, 255, 255),
                           .secondary = Rgba(244, 244, 245),
                           .secondaryForeground = Rgba(39, 39, 42),
                           .muted = Rgba(244, 244, 245),
                           .mutedForeground = Rgba(113, 113, 122),
                           .accent = Rgba(236, 253, 245),
                           .accentForeground = Rgba(0, 107, 61),
                           .destructive = Rgba(220, 38, 38),
                           .destructiveForeground = Rgba(255, 255, 255),
                           .border = Rgba(228, 228, 231),
                           .input = Rgba(212, 212, 216),
                           .ring = Rgba(0, 154, 89),
                           .success = Rgba(22, 163, 74),
                           .warning = Rgba(217, 119, 6)};
    }
    return ThemeTokens{.background = Rgba(9, 9, 11),
                       .foreground = Rgba(250, 250, 250),
                       .card = Rgba(16, 16, 20),
                       .popover = Rgba(24, 24, 27),
                       .primary = Rgba(0, 154, 89),
                       .primaryForeground = Rgba(255, 255, 255),
                       .secondary = Rgba(39, 39, 42),
                       .secondaryForeground = Rgba(250, 250, 250),
                       .muted = Rgba(24, 24, 27),
                       .mutedForeground = Rgba(161, 161, 170),
                       .accent = Rgba(5, 46, 34),
                       .accentForeground = Rgba(140, 238, 192),
                       .destructive = Rgba(239, 68, 68),
                       .destructiveForeground = Rgba(255, 255, 255),
                       .border = Rgba(39, 39, 42),
                       .input = Rgba(63, 63, 70),
                       .ring = Rgba(140, 238, 192),
                       .success = Rgba(34, 197, 94),
                       .warning = Rgba(245, 158, 11)};
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
                     .controlSm = 28.0F * safeScale,
                     .controlDefault = 34.0F * safeScale,
                     .controlLg = 38.0F * safeScale,
                     .controlRadius = 6.0F * safeScale,
                     .cardRadius = 10.0F * safeScale,
                     .popupRadius = 10.0F * safeScale,
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

} // namespace px::ui
