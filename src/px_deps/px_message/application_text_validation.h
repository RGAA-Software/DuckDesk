#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace px {

inline constexpr std::size_t kApplicationTextMaxBytes{16384};

// Strict UTF-8, no normalization. TAB/CR/LF are preserved; C0 and DEL rejected.
inline bool ValidApplicationText(const std::string_view text, const std::size_t max_bytes = kApplicationTextMaxBytes) {
    if (text.empty() || max_bytes == 0 || max_bytes > kApplicationTextMaxBytes || text.size() > max_bytes) {
        return false;
    }
    for (std::size_t index{}; index < text.size();) {
        const auto lead{static_cast<std::uint8_t>(text[index++])};
        std::uint32_t codepoint{lead};
        std::size_t continuation{};
        std::uint32_t minimum{};
        if (lead >= 0xc2 && lead <= 0xdf) {
            codepoint = lead & 0x1f;
            continuation = 1;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            codepoint = lead & 0x0f;
            continuation = 2;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            codepoint = lead & 0x07;
            continuation = 3;
            minimum = 0x10000;
        } else if (lead >= 0x80) {
            return false;
        }
        if (continuation > text.size() - index) {
            return false;
        }
        for (std::size_t offset{}; offset < continuation; ++offset) {
            const auto byte{static_cast<std::uint8_t>(text[index++])};
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint == 0x7f ||
            (codepoint < 0x20 && codepoint != 9 && codepoint != 10 && codepoint != 13)) {
            return false;
        }
    }
    return true;
}

inline bool ValidApplicationTextRequestId(const std::string_view request_id) {
    if (request_id.empty() || request_id.size() > 64) {
        return false;
    }
    for (const auto character : request_id) {
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
              character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

} // namespace px
