#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace px::ft {

inline constexpr std::size_t kSha256Size = 32;
using Sha256Digest = std::array<std::uint8_t, kSha256Size>;

class Sha256Hasher final {
public:
    Sha256Hasher();

    void Update(std::span<const std::uint8_t> bytes);
    [[nodiscard]] Sha256Digest Finalize();

private:
    void Transform(std::span<const std::uint8_t, 64> block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::uint64_t total_bytes_ = 0;
    std::size_t block_size_ = 0;
    bool finalized_ = false;
};

[[nodiscard]] std::string Sha256Bytes(const Sha256Digest& digest);

} // namespace px::ft
