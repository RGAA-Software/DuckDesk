#include "px_ui/device_platform.h"

namespace px::ui {

DevicePlatform ParseDevicePlatform(const std::string_view value) noexcept {
    if (value.empty() || value == "windows" || value == "win32" || value == "win64")
        return DevicePlatform::Windows;
    if (value == "macos" || value == "mac" || value == "darwin")
        return DevicePlatform::MacOS;
    if (value == "android")
        return DevicePlatform::Android;
    if (value == "ios" || value == "iphone" || value == "ipad")
        return DevicePlatform::IOS;
    return DevicePlatform::Unknown;
}

} // namespace px::ui
