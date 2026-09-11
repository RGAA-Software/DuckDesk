#pragma once

namespace px::ui {

enum class Theme {
    Dark,
    Light,
};

void ApplyPixelsTheme(Theme theme = Theme::Dark, float scale = 1.0F);
void ApplyPixelsColors(Theme theme);

} // namespace px::ui
