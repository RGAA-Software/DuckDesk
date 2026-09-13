#pragma once

#include <cstdint>
#include <string_view>

namespace px::ui {

enum class DevicePlatform : std::uint8_t { Unknown, Windows, MacOS, Android, IOS };

[[nodiscard]] DevicePlatform ParseDevicePlatform(std::string_view value) noexcept;

} // namespace px::ui
