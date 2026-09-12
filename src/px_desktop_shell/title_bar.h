#pragma once

namespace px::desktop {

class WindowHost;
struct WindowChromeConfig;

bool DrawTitleBar(WindowHost& window, const WindowChromeConfig& chrome);

} // namespace px::desktop
