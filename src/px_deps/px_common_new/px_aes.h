#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace px {

inline constexpr std::size_t kAes128KeySize = 16;
inline constexpr std::size_t kAes128IvSize = 16;

bool AesEncryptPcks7Cbc128(std::span<const std::byte> plaintext, std::span<const std::byte> key, std::span<const std::byte> iv,
                           std::vector<unsigned char>& ciphertext);

bool AesDecryptPcks7Cbc128(std::span<const std::byte> ciphertext, std::span<const std::byte> key, std::span<const std::byte> iv,
                           std::vector<unsigned char>& plaintext);

} // namespace px
