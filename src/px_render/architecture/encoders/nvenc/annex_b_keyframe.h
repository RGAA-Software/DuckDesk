#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace px {

enum class AnnexBVideoCodec : std::uint8_t { kH264, kH265 };

// Returns nullopt when the buffer has no Annex-B VCL NAL unit. Otherwise the value states whether the access unit is random-access.
[[nodiscard]] inline std::optional<bool> DetectAnnexBRandomAccess(const std::span<const std::uint8_t> bitstream,
                                                                  const AnnexBVideoCodec codec) {
    std::optional<bool> has_non_random_vcl{};
    for (std::size_t index{}; index + 3 < bitstream.size();) {
        std::size_t header{};
        if (bitstream[index] == 0 && bitstream[index + 1] == 0 && bitstream[index + 2] == 1) {
            header = index + 3;
        } else if (index + 4 < bitstream.size() && bitstream[index] == 0 && bitstream[index + 1] == 0 && bitstream[index + 2] == 0 &&
                   bitstream[index + 3] == 1) {
            header = index + 4;
        } else {
            ++index;
            continue;
        }
        if (header >= bitstream.size()) {
            break;
        }
        if (codec == AnnexBVideoCodec::kH264) {
            const auto nal_type = static_cast<std::uint8_t>(bitstream[header] & 0x1FU);
            if (nal_type == 5) {
                return true;
            }
            if (nal_type >= 1 && nal_type <= 4) {
                has_non_random_vcl = false;
            }
        } else {
            const auto nal_type = static_cast<std::uint8_t>((bitstream[header] >> 1U) & 0x3FU);
            if (nal_type >= 16 && nal_type <= 21) {
                return true;
            }
            if (nal_type <= 31) {
                has_non_random_vcl = false;
            }
        }
        index = header + 1;
    }
    return has_non_random_vcl;
}

} // namespace px
