#pragma once

namespace px::ui {

enum class Theme {
    Dark,
    Light,
};

void ApplyPixelsTheme(Theme theme = Theme::Dark, float scale = 1.0F, bool enhancedVisualEffects = true);
void ApplyPixelsColors(Theme theme);
[[nodiscard]] bool EnhancedVisualEffectsEnabled() noexcept;

} // namespace px::ui
