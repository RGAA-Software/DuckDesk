#pragma once

#include <cstdint>
#include <string_view>

namespace px::client::imgui {

[[nodiscard]] std::uint32_t WindowsVirtualKey(std::int32_t sdlKey) noexcept;
[[nodiscard]] bool ContainsNonAscii(std::string_view text) noexcept;

} // namespace px::client::imgui
