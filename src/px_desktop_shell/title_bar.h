#pragma once

#include <string_view>

namespace px::desktop {

class WindowHost;
class BrandLogo;
struct WindowChromeConfig;

inline constexpr int kTitleBarLogicalHeight{40};
inline constexpr int kCaptionButtonLogicalWidth{40};

bool DrawTitleBar(WindowHost& window, const WindowChromeConfig& chrome, const BrandLogo& logo, std::string_view titleOverride = {});

} // namespace px::desktop
