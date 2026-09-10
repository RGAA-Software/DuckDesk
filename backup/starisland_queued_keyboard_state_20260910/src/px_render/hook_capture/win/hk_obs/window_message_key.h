#pragma once

#include <Windows.h>
#include <cstdint>

namespace px {
// WM_KEYDOWN/UP use generic modifier VKs. The scan code and extended bit
// retain the side; Raw Input and the protocol retain their original key.
[[nodiscard]] constexpr std::uint32_t WindowMessageKey(std::uint32_t key) noexcept {
    switch (key) {
    case VK_LSHIFT:
    case VK_RSHIFT:
        return VK_SHIFT;
    case VK_LCONTROL:
    case VK_RCONTROL:
        return VK_CONTROL;
    case VK_LMENU:
    case VK_RMENU:
        return VK_MENU;
    default:
        return key;
    }
}
} // namespace px
