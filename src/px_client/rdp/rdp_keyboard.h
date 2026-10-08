#pragma once

#include <cstdint>

namespace px::rdp {

// Windows uses an E0 prefix; FreeRDP represents the same prefix with KBDEXT (bit 8).
[[nodiscard]] constexpr std::uint32_t RdpScanCodeFromWindows(const std::uint32_t windowsScanCode) noexcept {
    const bool extended{(windowsScanCode & 0xFF00U) == 0xE000U};
    return (windowsScanCode & 0xFFU) | (extended ? 0x100U : 0U);
}

} // namespace px::rdp
