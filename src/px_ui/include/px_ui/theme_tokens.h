#pragma once
#include "px_ui/px_ui_theme.h"

#include <imgui.h>

namespace px::ui {

struct ThemeTokens final {
    ImVec4 background{};
    ImVec4 foreground{};
    ImVec4 card{};
    ImVec4 popover{};
    ImVec4 primary{};
    ImVec4 primaryForeground{};
    ImVec4 secondary{};
    ImVec4 secondaryForeground{};
    ImVec4 muted{};
    ImVec4 mutedForeground{};
    ImVec4 accent{};
    ImVec4 accentForeground{};
    ImVec4 destructive{};
    ImVec4 destructiveForeground{};
    ImVec4 border{};
    ImVec4 input{};
    ImVec4 ring{};
    ImVec4 success{};
    ImVec4 warning{};
};

struct UiMetrics final {
    float scale{1.0F};
    float controlXs{24.0F};
    float controlSm{32.0F};
    float controlDefault{36.0F};
    float controlLg{40.0F};
    float controlRadius{6.0F};
    float cardRadius{10.0F};
    float popupRadius{10.0F};
    float borderWidth{1.0F};
    float spacingXs{4.0F};
    float spacingSm{8.0F};
    float spacingMd{12.0F};
    float spacingLg{16.0F};
    float spacingXl{24.0F};
    float iconSm{14.0F};
    float iconDefault{16.0F};
    float iconLg{20.0F};
};

[[nodiscard]] ThemeTokens ThemeTokensFor(Theme theme) noexcept;
[[nodiscard]] ThemeTokens CurrentThemeTokens() noexcept;
[[nodiscard]] UiMetrics MetricsFor(float scale) noexcept;

} // namespace px::ui
