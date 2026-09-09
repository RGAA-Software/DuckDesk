#pragma once

#include <algorithm>
#include <span>
#include <string_view>

namespace px {

// A game-hook Render owns one virtual display. Older hook IPC frames omit a
// physical monitor name; assign a stable identity before routing or encoding.
inline bool EnsureGameFrameIdentity(std::span<char> display_name) {
    if (display_name.empty()) {
        return false;
    }
    if (display_name.front() != '\0') {
        return std::find(display_name.begin(), display_name.end(), '\0') != display_name.end();
    }
    constexpr std::string_view identity{"game-hook"};
    if (display_name.size() <= identity.size()) {
        return false;
    }
    std::fill(display_name.begin(), display_name.end(), '\0');
    std::copy(identity.begin(), identity.end(), display_name.begin());
    return true;
}

} // namespace px
