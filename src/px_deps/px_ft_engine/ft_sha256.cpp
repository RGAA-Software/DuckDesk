#include "ft_sha256.h"

#include <algorithm>
#include <stdexcept>

namespace px::ft {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::uint32_t RotateRight(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

} // namespace

Sha256Hasher::Sha256Hasher()
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256Hasher::Update(std::span<const std::uint8_t> bytes) {
    if (finalized_) throw std::logic_error("SHA-256 hasher already finalized");
    total_bytes_ += bytes.size();
    while (!bytes.empty()) {
        const auto count = std::min(block_.size() - block_size_, bytes.size());
        std::copy_n(bytes.begin(), count, block_.begin() + block_size_);
        block_size_ += count;
        bytes = bytes.subspan(count);
        if (block_size_ == block_.size()) {
            Transform(std::span<const std::uint8_t, 64>(block_));
            block_size_ = 0;
        }
    }
}

Sha256Digest Sha256Hasher::Finalize() {
    if (finalized_) throw std::logic_error("SHA-256 hasher already finalized");
    const std::uint64_t bit_length = total_bytes_ * 8U;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56) {
        std::fill(block_.begin() + block_size_, block_.end(), 0U);
        Transform(std::span<const std::uint8_t, 64>(block_));
        block_size_ = 0;
    }
    std::fill(block_.begin() + block_size_, block_.begin() + 56, 0U);
    for (std::size_t index = 0; index < 8; ++index) {
        block_[63 - index] = static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    Transform(std::span<const std::uint8_t, 64>(block_));
    finalized_ = true;

    Sha256Digest digest{};
    for (std::size_t index = 0; index < state_.size(); ++index) {
        digest[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24U);
        digest[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16U);
        digest[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8U);
        digest[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
    }
    return digest;
}

void Sha256Hasher::Transform(std::span<const std::uint8_t, 64> block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                       (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
                       (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
                       static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto s0 = RotateRight(words[index - 15], 7) ^
                        RotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3U);
        const auto s1 = RotateRight(words[index - 2], 17) ^
                        RotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto temp1 = h + sum1 + choose + kRoundConstants[index] + words[index];
        const auto sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::string Sha256Bytes(const Sha256Digest& digest) {
    std::string bytes;
    bytes.reserve(digest.size());
    for (const auto value : digest) {
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

} // namespace px::ft
