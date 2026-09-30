#pragma once

#include <string_view>

#include "px_ui/localization.h"

namespace px::desktop {

class WindowHost;
class BrandLogo;
struct WindowChromeConfig;

inline constexpr int kTitleBarLogicalHeight{40};
inline constexpr int kCaptionButtonLogicalWidth{40};

bool DrawTitleBar(WindowHost& window, const WindowChromeConfig& chrome, const BrandLogo& logo, const px::ui::Localizer& localizer,
                  std::string_view titleOverride = {});

}  // namespace px::desktop
