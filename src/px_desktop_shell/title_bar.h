#pragma once

namespace px::desktop {

class WindowHost;
class BrandLogo;
struct WindowChromeConfig;

inline constexpr int kTitleBarLogicalHeight{40};
inline constexpr int kCaptionButtonLogicalWidth{40};

bool DrawTitleBar(WindowHost& window, const WindowChromeConfig& chrome, const BrandLogo& logo);

} // namespace px::desktop
