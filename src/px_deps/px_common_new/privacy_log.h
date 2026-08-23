#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace px {

// Produces a stable short correlation token without exposing the source value.
// This is intended for identifiers in diagnostics, never for authentication.
inline std::string PrivacyLogId(std::string_view value) {
    uint32_t hash = 2'166'136'261u;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 16'777'619u;
    }
    std::array<char, 8> text{};
    constexpr char digits[] = "0123456789abcdef";
    for (int index = 7; index >= 0; --index) {
        text[static_cast<size_t>(index)] = digits[hash & 0xfu];
        hash >>= 4;
    }
    return {text.data(), text.size()};
}

}  // namespace px
