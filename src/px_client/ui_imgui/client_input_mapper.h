#pragma once

#include <cstdint>
#include <string_view>

namespace px::client::imgui {

struct WindowsKey final {
    std::uint32_t virtualKey{};
    std::uint32_t scanCode{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return virtualKey != 0U && scanCode != 0U;
    }
};

[[nodiscard]] WindowsKey WindowsKeyFromSdl(std::int32_t sdlKey, std::int32_t sdlScanCode, std::uint16_t platformScanCode = 0) noexcept;
[[nodiscard]] std::uint32_t WindowsVirtualKey(std::int32_t sdlKey, std::int32_t sdlScanCode = 0) noexcept;
[[nodiscard]] bool ContainsNonAscii(std::string_view text) noexcept;

} // namespace px::client::imgui
